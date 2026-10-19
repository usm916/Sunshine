/**
 * @file src/video_low_bandwidth.h
 * @brief Per-session low-bandwidth budgets, frame admission, and congestion recovery.
 */
#pragma once

#include "video_cursor_focus.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace video::low_bandwidth {
  using clock_t = std::chrono::steady_clock;  ///< Monotonic scheduling clock.
  using time_point_t = clock_t::time_point;  ///< Monotonic sample timestamp.

  /**
   * @brief User settings copied into each connection; disabled preserves normal streaming.
   */
  struct settings_t {
    std::string mode = "disabled";  ///< disabled, fixed, or adaptive.
    int target_kbps = 0;  ///< Optional total video-path ceiling; zero follows the client request.
    int minimum_kbps = 100;  ///< Lowest total budget during congestion.
    int maximum_fps = 15;  ///< Maximum encoded frame frequency, bounded by the client request.
    int minimum_fps = 2;  ///< Lowest encoded frame frequency.
  };

  /**
   * @brief One atomic encoder-thread update, expressed without changing the wire protocol.
   */
  struct profile_t {
    std::uint32_t wire_kbps = 0;  ///< Estimated total video-path budget.
    std::uint32_t video_kbps = 0;  ///< Codec target after conservative overhead reservation.
    int fps = 0;  ///< Actual admission rate; negotiated client rate stays unchanged.
    bool audio_enabled = false;  ///< Audio allowance is reserved before enabling the session's audio gate.
    bool coarse_background = false;  ///< Stateful background policy with separate entry and exit budgets.
    /**
     * @brief Compare all fields of two indivisible encoder profiles.
     */
    bool operator==(const profile_t &) const = default;
  };

  /**
   * @brief Sender observations shared with the encoder and control workers without locks.
   */
  struct telemetry_t {
    std::atomic<std::uint64_t> wire_bytes {0};  ///< Media IP bytes submitted, including video/audio FEC and UDP/IP headers.
    std::atomic<std::uint64_t> last_frame_bytes {0};  ///< Most recently submitted complete encoded frame.
    std::atomic<int> reconfigure_status {0};  ///< Zero unknown, one supported, minus one failed or unsupported.
  };

  /**
   * @brief Resolve an optional server cap without silently overriding the client with 500 kbps.
   * @param settings Session limits; target zero follows the request.
   * @param requested Client bitrate before FEC and audio reservations.
   * @param global_cap Optional global maximum bitrate, or zero.
   * @return Effective total video-path ceiling, bounded to supported limits.
   */
  inline std::uint32_t select_ceiling(const settings_t &settings, std::int64_t requested, int global_cap = 0) {
    auto ceiling = std::clamp<std::int64_t>(requested, 32, 1000000);
    if (settings.target_kbps > 0) {
      ceiling = std::min(ceiling, static_cast<std::int64_t>(settings.target_kbps));
    }
    if (global_cap > 0) {
      ceiling = std::min(ceiling, static_cast<std::int64_t>(global_cap));
    }
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(ceiling, 32, 1000000));
  }

  /**
   * @brief Select an encoded frequency and reserve minimum FEC and transport costs.
   * @param settings Session limits.
   * @param budget Total audio/video transport budget in kbps.
   * @param client_fps Client's negotiated frame rate.
   * @param packet_bytes Approximate per-packet IP size.
   * @param minimum_parity Minimum parity shards per frame, or zero with FEC disabled.
   * @param fec_percent Nominal FEC percentage.
   * @param audio_kbps Potential audio transport allowance, including its FEC and headers.
   * @param previous Previously requested profile, or null for initial stateless selection.
   * @return Conservative encoder profile; not a strict packet-level shaper.
   */
  inline profile_t make_profile(const settings_t &settings, std::uint32_t budget, int client_fps, int packet_bytes = 1432, int minimum_parity = 2, int fec_percent = 20, std::uint32_t audio_kbps = 0, const profile_t *previous = nullptr) {
    budget = std::clamp<std::uint32_t>(budget, 32, 1000000);
    const auto audio_threshold = previous && previous->audio_enabled ? 2700U : 3000U;
    const bool audio_enabled = audio_kbps > 0 && budget >= audio_threshold && static_cast<std::uint64_t>(audio_kbps) + 1000 <= budget;
    const bool coarse = budget < (previous && !previous->coarse_background ? 2700U : 3000U);
    const auto video_budget = budget - (audio_enabled ? audio_kbps : 0);
    const int maximum = std::clamp(settings.maximum_fps, 1, std::max(1, client_fps));
    const int minimum = std::clamp(settings.minimum_fps, 1, maximum);
    int fps = budget <= 250 ? 5 : budget <= 500 ? 8 :
                                budget <= 1000  ? 10 :
                                budget <= 2000  ? 15 :
                                                  maximum;
    fps = std::clamp(fps, minimum, maximum);
    const auto parity_cost = [&](int rate) {
      return static_cast<std::uint64_t>(std::max(0, minimum_parity)) * std::clamp(packet_bytes, 64, 65536) * 8 * rate / 1000;
    };
    while (fps > minimum && parity_cost(fps) > video_budget * 2ULL / 5) {
      --fps;
    }
    const auto reserved = std::min<std::uint64_t>(video_budget - 8, 16 + parity_cost(fps));
    const auto payload = (video_budget - reserved) * 80ULL / (100 + std::clamp(fec_percent, 0, 255));
    return {budget, static_cast<std::uint32_t>(std::max<std::uint64_t>(8, payload)), fps, audio_enabled, coarse};
  }

  /**
   * @brief Preserve manual focus geometry while linking background detail to the current budget.
   * @param base User-selected focus geometry.
   * @param budget Current total video budget.
   * @return Background preset for this connection.
   */
  inline cursor_focus::settings_t focus_settings(cursor_focus::settings_t base, std::uint32_t budget) {
    base.enabled = true;
    base.automatic = true;
    return cursor_focus::select_settings(std::move(base), budget);
  }

  /**
   * @brief Apply the hysteretic presentation policy of an indivisible profile.
   * @param base User focus geometry.
   * @param profile Requested transport, audio and coarse-background policy.
   * @return Effective focus settings without a threshold toggle inside the dead band.
   */
  inline cursor_focus::settings_t focus_settings(cursor_focus::settings_t base, const profile_t &profile) {
    auto result = focus_settings(std::move(base), profile.wire_kbps);
    result.enabled = profile.coarse_background;
    return result;
  }

  /**
   * @brief One control-thread measurement; silence never proves a healthy connection.
   */
  struct sample_t {
    std::uint32_t rtt_ms = 0;  ///< Smoothed control RTT.
    std::uint32_t rtt_token = 0;  ///< Last received control-message token.
    std::uint64_t loss_events = 0;  ///< Unrecovered data packets, separate from decoder recovery requests.
    std::uint64_t recovered_packets = 0;  ///< Data reconstructed by FEC.
    bool reliable_reports = true;  ///< False for malformed, protocol-mismatched or rate-limited telemetry.
    std::uint32_t rtt_variance_ms = 0;  ///< ENet control-path RTT variation.
    std::uint64_t frame_loss_requests = 0;  ///< Decoder IDR or reference-frame recovery requests.
    std::uint64_t incomplete_reports = 0;  ///< FEC reports without initialized packet counts.
  };

  /**
   * @brief Duration-weighted, three-second sender history without heap allocation.
   */
  class sender_window_t {
  public:
    /**
     * @brief Discard measurements from the previous target or a sampling gap.
     */
    void reset() {
      buckets_ = {};
      first_ = count_ = duration_ms_ = 0;
      weighted_kbps_ms_ = 0;
    }

    /**
     * @brief Add a rate measured over at least one controller interval.
     * @param rate_kbps Submitted media rate, bounded to the supported maximum.
     * @param elapsed Actual interval duration; intervals are bounded to 500-3000ms.
     */
    void observe(std::uint32_t rate_kbps, clock_t::duration elapsed) {
      const auto duration = static_cast<unsigned>(std::clamp<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 500, 3000));
      rate_kbps = std::min(rate_kbps, 1000000U);
      buckets_[(first_ + count_) % buckets_.size()] = {rate_kbps, duration};
      ++count_;
      duration_ms_ += duration;
      weighted_kbps_ms_ += static_cast<std::uint64_t>(rate_kbps) * duration;
      while (duration_ms_ > 3000) {
        remove(std::min(duration_ms_ - 3000, buckets_[first_].duration_ms));
      }
    }

    /**
     * @brief Return whether a complete three-second interval has been measured.
     * @return Window readiness.
     */
    bool ready() const {
      return duration_ms_ == 3000;
    }

    /**
     * @brief Return the duration-weighted submitted rate.
     * @return Average kbps, or zero without samples.
     */
    std::uint32_t average_kbps() const {
      return duration_ms_ ? static_cast<std::uint32_t>(weighted_kbps_ms_ / duration_ms_) : 0;
    }

  private:
    /**
     * @brief One constant-rate measurement interval.
     */
    struct bucket_t {
      std::uint32_t rate_kbps = 0;  ///< Submitted rate during this interval.
      unsigned duration_ms = 0;  ///< Remaining duration inside the rolling window.
    };

    /**
     * @brief Trim the oldest interval and release an exhausted bucket.
     * @param duration Duration to remove from the oldest bucket.
     */
    void remove(unsigned duration) {
      auto &oldest = buckets_[first_];
      duration_ms_ -= duration;
      weighted_kbps_ms_ -= static_cast<std::uint64_t>(oldest.rate_kbps) * duration;
      oldest.duration_ms -= duration;
      if (oldest.duration_ms == 0) {
        first_ = (first_ + 1) % buckets_.size();
        --count_;
      }
    }

    std::array<bucket_t, 7> buckets_ {};  ///< Six 500ms intervals plus a partially expired interval.
    std::size_t first_ = 0;  ///< Oldest occupied bucket.
    std::size_t count_ = 0;  ///< Occupied buckets.
    unsigned duration_ms_ = 0;  ///< Total retained duration, bounded to three seconds.
    std::uint64_t weighted_kbps_ms_ = 0;  ///< Sum of rate multiplied by retained milliseconds.
  };

  /**
   * @brief Bit flags describing current recovery blockers.
   */
  enum hold_e : unsigned {
    loss = 1,  ///< Unrecovered loss or a frame recovery request.
    latency = 2,  ///< RTT remains above the accepted path baseline.
    stalled = 4,  ///< No fresh positive RTT for two seconds.
    sender_pressure = 8,  ///< Sustained sender average exceeds its budget.
    unreliable = 16,  ///< Telemetry cannot certify recovery.
    sampling_gap = 32,  ///< The control loop did not sample within 1500ms.
    recovery_request = 64,  ///< A soft decoder recovery signal; isolated events do not restart recovery.
  };

  /**
   * @brief Read-only controller state for bounded diagnostic logging.
   */
  struct diagnostics_t {
    unsigned holds = 0;  ///< Combination of hold_e flags.
    std::uint32_t baseline_rtt_ms = 0;  ///< Accepted path RTT baseline.
    std::uint32_t sender_average_kbps = 0;  ///< Duration-weighted three-second sender rate.
    std::uint32_t baseline_revision = 0;  ///< Count of accepted path baseline changes.
    std::uint64_t healthy_ms = 0;  ///< Duration of the current healthy period.
    std::uint64_t ack_age_ms = 0;  ///< Time since a fresh positive RTT observation.
    bool sender_ready = false;  ///< Whether a full sender interval is available.
    bool adaptation_stopped = false;  ///< Encoder reconfiguration failed or is unsupported.
    bool probe_active = false;  ///< A tentative media target is being observed.
    std::uint32_t verified_target_kbps = 0;  ///< Last probe supported by substantial actual sender load; zero is unknown.
    std::uint64_t unrecovered_packets = 0;  ///< Current window's unrecovered data packets.
    std::uint64_t frame_loss_requests = 0;  ///< Current window's decoder recovery requests.
    std::uint64_t incomplete_reports = 0;  ///< Current window's reports without packet counts.
  };

  /**
   * @brief Log reason transitions at most once per two seconds, without periodic stable-state logs.
   */
  class diagnostic_gate_t {
  public:
    /**
     * @brief Coalesce reason changes while retaining the latest state for the next eligible call.
     * @param value Current controller diagnostics.
     * @param now Monotonic observation time.
     * @return Whether a changed state should be logged now.
     */
    bool should_log(const diagnostics_t &value, time_point_t now) {
      using namespace std::chrono_literals;
      if (last_at_ && (now < *last_at_ || now - *last_at_ < 2s)) {
        return false;
      }
      if (last_ && last_->holds == value.holds && last_->baseline_revision == value.baseline_revision && last_->sender_ready == value.sender_ready && last_->adaptation_stopped == value.adaptation_stopped && last_->probe_active == value.probe_active && last_->verified_target_kbps == value.verified_target_kbps) {
        return false;
      }
      last_ = value;
      last_at_ = now;
      return true;
    }

  private:
    std::optional<diagnostics_t> last_;  ///< Last reported reason signature.
    std::optional<time_point_t> last_at_;  ///< Last diagnostic publication time.
  };

  /**
   * @brief Fixed-storage RTT history used to follow a route rather than a lifetime minimum.
   */
  class route_window_t {
  public:
    /** @brief Discard observations across unreliable feedback or a sampling gap. */
    void reset() {
      count_ = first_ = 0;
    }

    /**
     * @brief Add one fresh RTT and expire entries older than thirty seconds.
     * @param now Monotonic receipt time.
     * @param rtt Fresh positive RTT.
     */
    void observe(time_point_t now, std::uint32_t rtt) {
      using namespace std::chrono_literals;
      while (count_ && now - entries_[first_].at > 30s) {
        first_ = (first_ + 1) % entries_.size();
        --count_;
      }
      if (count_ == entries_.size()) {
        first_ = (first_ + 1) % entries_.size();
        --count_;
      }
      entries_[(first_ + count_) % entries_.size()] = {now, rtt};
      ++count_;
    }

    /**
     * @brief Read a lower percentile only after ten seconds of route evidence.
     * @param now Current monotonic time.
     * @param variation Current ENet RTT variation.
     * @return Lower-quintile RTT, or no value for insufficient or excessively noisy evidence.
     */
    std::optional<std::uint32_t> baseline(time_point_t now, std::uint32_t variation) const {
      using namespace std::chrono_literals;
      if (count_ < 6 || now - entries_[first_].at < 10s) {
        return std::nullopt;
      }
      std::array<std::uint32_t, 64> values {};
      for (std::size_t i = 0; i < count_; ++i) {
        values[i] = entries_[(first_ + i) % entries_.size()].rtt;
      }
      std::sort(values.begin(), values.begin() + count_);
      const auto lower = values[(count_ - 1) / 5];
      const auto upper = values[(count_ - 1) * 9 / 10];
      const auto tolerance = std::max({40U, lower / 2, std::min(variation, 30U) * 4});
      return upper - lower <= tolerance ? std::optional {lower} : std::nullopt;
    }

  private:
    /** @brief One fresh control-path RTT. */
    struct entry_t {
      time_point_t at {};  ///< Observation time.
      std::uint32_t rtt = 0;  ///< Positive RTT in milliseconds.
    };

    std::array<entry_t, 64> entries_ {};  ///< At most thirty-two seconds of 500-ms samples.
    std::size_t first_ = 0;  ///< Oldest entry.
    std::size_t count_ = 0;  ///< Occupied entries.
  };

  /**
   * @brief Reason-specific backoff and bounded media probes, independent of encoded FPS.
   */
  class controller_t {
  public:
    /**
     * @brief Reset per-connection evidence without assuming the requested ceiling is measured capacity.
     * @param settings Connection policy.
     * @param ceiling Requested maximum total media budget.
     * @param now Session start time.
     */
    void initialize(settings_t settings, std::uint32_t ceiling, time_point_t now) {
      settings_ = std::move(settings);
      ceiling_ = std::clamp(ceiling, 32U, 1000000U);
      floor_ = std::min(ceiling_, static_cast<std::uint32_t>(std::clamp(settings_.minimum_kbps, 32, 1000000)));
      target_ = ceiling_;
      baseline_ = token_ = bad_windows_ = recovery_score_ = latency_cuts_ = probe_failures_ = 0;
      last_tick_ = healthy_since_ = last_fresh_ = baseline_at_ = next_probe_ = soft_until_ = now;
      latency_since_.reset();
      last_latency_cut_.reset();
      probe_since_.reset();
      verified_at_.reset();
      probe_base_ = 0;
      sender_.reset();
      route_.reset();
      diagnostics_ = {};
    }

    /**
     * @brief Observe a completed window and optionally change the media target.
     * @param sample Separated control and decoder observations.
     * @param now Monotonic publication time.
     * @param submitted_kbps Actual submitted media rate, including transport overhead.
     * @return Requested total target, or no change.
     */
    std::optional<std::uint32_t> observe(const sample_t &sample, time_point_t now, std::uint32_t submitted_kbps) {
      using namespace std::chrono_literals;
      if (settings_.mode != "adaptive" || now < last_tick_ || now - last_tick_ < 500ms) {
        return std::nullopt;
      }
      const auto elapsed = now - last_tick_;
      last_tick_ = now;
      const bool continuous = elapsed <= 1500ms && now - last_fresh_ < 2s;
      if (elapsed > 1500ms) {
        sender_.reset();
      }
      sender_.observe(submitted_kbps, elapsed);
      const bool fresh = sample.rtt_token != 0 && sample.rtt_token != token_ && sample.rtt_ms > 0;
      token_ = sample.rtt_token;
      if (fresh) {
        last_fresh_ = now;
        if (!baseline_) {
          baseline_ = sample.rtt_ms;
          baseline_at_ = now;
        }
      }
      const bool wire_pressure = sender_.ready() && (sender_.average_kbps() > target_ * 12ULL / 10 ||
                                                     (sample.recovered_packets > 0 && sender_.average_kbps() > target_));
      const bool soft = sample.frame_loss_requests > 0 || sample.incomplete_reports > 0;
      if (soft) {
        soft_until_ = now + 1s;
      }
      recovery_score_ = soft ? std::min(8U, recovery_score_ + 2) : (recovery_score_ ? recovery_score_ - 1 : 0);
      const bool lost = sample.loss_events > 0 || recovery_score_ >= 4;
      const bool stalled_control = now - last_fresh_ >= 2s;
      const bool hard = lost || stalled_control || wire_pressure;
      const bool trustworthy = sample.reliable_reports && continuous;
      if (!trustworthy || hard || sample.recovered_packets > 0 || !sender_.ready() || sender_.average_kbps() > target_) {
        route_.reset();
      } else if (fresh) {
        route_.observe(now, sample.rtt_ms);
        if (const auto candidate = route_.baseline(now, sample.rtt_variance_ms)) {
          const bool lower = *candidate < baseline_ && baseline_ - *candidate > std::max(4U, baseline_ / 10);
          const bool higher = *candidate > baseline_ && now - baseline_at_ >= 30s && (latency_cuts_ || target_ == floor_);
          if (lower || higher) {
            baseline_ = *candidate;
            baseline_at_ = now;
            latency_cuts_ = 0;
            ++diagnostics_.baseline_revision;
          }
        }
      }
      const bool delayed = fresh && static_cast<std::uint64_t>(sample.rtt_ms) > static_cast<std::uint64_t>(baseline_) +
                                                                                  std::max({45U, baseline_ / 2, std::min(sample.rtt_variance_ms, 30U) * 4});
      if (!trustworthy || hard) {
        latency_since_.reset();
      } else if (fresh) {
        if (delayed) {
          if (!latency_since_) {
            latency_since_ = now;
          }
        } else {
          latency_since_.reset();
        }
      }
      if (hard || !trustworthy || delayed) {
        healthy_since_ = now;
      }
      if (!trustworthy) {
        probe_since_.reset();
        next_probe_ = now + 3s;
      }
      if (verified_at_ && now - *verified_at_ > 30s) {
        diagnostics_.verified_target_kbps = 0;
      }
      diagnostics_.holds = (lost ? loss : 0U) | (delayed ? latency : 0U) | (stalled_control ? stalled : 0U) |
                           (wire_pressure ? sender_pressure : 0U) | (!sample.reliable_reports ? unreliable : 0U) |
                           (elapsed > 1500ms ? sampling_gap : 0U) | (soft ? recovery_request : 0U);
      diagnostics_.baseline_rtt_ms = baseline_;
      diagnostics_.sender_average_kbps = sender_.average_kbps();
      diagnostics_.sender_ready = sender_.ready();
      diagnostics_.healthy_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - healthy_since_).count();
      diagnostics_.ack_age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fresh_).count();
      diagnostics_.unrecovered_packets = sample.loss_events;
      diagnostics_.frame_loss_requests = sample.frame_loss_requests;
      diagnostics_.incomplete_reports = sample.incomplete_reports;
      std::optional<std::uint32_t> changed;
      if (hard) {
        if (++bad_windows_ >= 2) {
          bad_windows_ = 0;
          const auto reduced = std::max(floor_, std::min(target_ * 3 / 4, probe_since_ ? probe_base_ : target_));
          changed = reduce(reduced, now);
        }
      } else {
        bad_windows_ = 0;
        if (latency_since_ && now - *latency_since_ >= 3s && latency_cuts_ < 2 && (!last_latency_cut_ || now - *last_latency_cut_ >= 4s)) {
          ++latency_cuts_;
          last_latency_cut_ = now;
          changed = reduce(std::max(floor_, std::min(target_ * 9 / 10, probe_since_ ? probe_base_ : target_)), now);
        } else if (fresh && trustworthy && !delayed && !soft && now >= soft_until_) {
          if (now - healthy_since_ >= 10s) {
            latency_cuts_ = 0;
          }
          if (probe_since_ && now - *probe_since_ >= 3s && now - healthy_since_ >= 3s) {
            if (sender_.ready() && sender_.average_kbps() >= target_ / 2) {
              diagnostics_.verified_target_kbps = target_;
              verified_at_ = now;
            }
            probe_since_.reset();
            probe_failures_ = 0;
          }
          if (!probe_since_ && now >= next_probe_ && now - healthy_since_ >= 3s && target_ < ceiling_) {
            probe_base_ = target_;
            const auto increase = target_ < 1000 ? target_ : std::min(2048U, std::max(128U, target_ / 4));
            target_ = std::min(ceiling_, target_ + increase);
            probe_since_ = now;
            next_probe_ = now + 3s;
            changed = target_;
          }
        }
      }
      diagnostics_.probe_active = probe_since_.has_value();
      return changed;
    }

    /** @brief Return the latest sampled reasons and probe evidence. @return Diagnostic state. */
    const diagnostics_t &diagnostics() const {
      return diagnostics_;
    }

    /** @brief Return the tentative or settled media target. @return Total budget in kbps. */
    std::uint32_t target() const {
      return target_;
    }

  private:
    /**
     * @brief Revert a failed probe or reduce pressure, then enforce a retry cooldown.
     * @param reduced Bounded target selected for this reason.
     * @param now Reduction time.
     * @return A changed target only when it actually decreases.
     */
    std::optional<std::uint32_t> reduce(std::uint32_t reduced, time_point_t now) {
      using namespace std::chrono_literals;
      if (probe_since_) {
        probe_failures_ = std::min(4U, probe_failures_ + 1);
      }
      probe_since_.reset();
      const auto cooldown = probe_failures_ ? std::min(30U, 4U << (probe_failures_ - 1)) : 4U;
      next_probe_ = now + std::chrono::seconds(cooldown);
      if (reduced >= target_) {
        return std::nullopt;
      }
      target_ = reduced;
      diagnostics_.verified_target_kbps = 0;
      verified_at_.reset();
      sender_.reset();
      return target_;
    }

    settings_t settings_;  ///< Immutable connection policy.
    std::uint32_t ceiling_ = 0;  ///< User/client upper bound.
    std::uint32_t floor_ = 0;  ///< Lowest allowed total budget.
    std::uint32_t target_ = 0;  ///< Current tentative or settled target.
    std::uint32_t baseline_ = 0;  ///< Lower-percentile RTT for the recent route.
    std::uint32_t token_ = 0;  ///< Previous receipt token.
    unsigned bad_windows_ = 0;  ///< Consecutive windows of strong congestion evidence.
    unsigned recovery_score_ = 0;  ///< Decaying score distinguishing isolated and repeated decoder requests.
    unsigned latency_cuts_ = 0;  ///< Limited delay-only reductions before checking route evidence.
    unsigned probe_failures_ = 0;  ///< Saturating exponential retry backoff after rejected probes.
    std::uint32_t probe_base_ = 0;  ///< Target before the pending probe, not a capacity measurement.
    time_point_t last_tick_ {};  ///< Last processed sample.
    time_point_t healthy_since_ {};  ///< Last hard, delayed or unreliable sample.
    time_point_t last_fresh_ {};  ///< Last new positive RTT token.
    time_point_t baseline_at_ {};  ///< Last accepted route baseline.
    time_point_t next_probe_ {};  ///< Earliest next probe or retry.
    time_point_t soft_until_ {};  ///< One-second observation pause after an isolated decoder request.
    std::optional<time_point_t> latency_since_;  ///< Current continuous RTT excursion.
    std::optional<time_point_t> last_latency_cut_;  ///< Cooldown for delay-only reductions.
    std::optional<time_point_t> probe_since_;  ///< Beginning of the tentative target's observation period.
    std::optional<time_point_t> verified_at_;  ///< Expiry for historical loaded-probe evidence.
    sender_window_t sender_;  ///< Weighted sender load, independent of FPS.
    route_window_t route_;  ///< Fixed-storage recent RTT history.
    diagnostics_t diagnostics_;  ///< Latest sampled reasons and verified-load evidence.
  };

  /**
   * @brief Admit frames before encoding and avoid catch-up bursts after stalls.
   */
  class frame_gate_t {
  public:
    /**
     * @brief Check when the next frame may be encoded.
     * @param now Current time.
     * @return Remaining wait, or zero when a frame may be encoded.
     */
    clock_t::duration wait(time_point_t now) const {
      return now < next_ ? next_ - now : clock_t::duration::zero();
    }

    /**
     * @brief Schedule the next frame using FPS and the previous frame's measured wire cost.
     * @param now Current encode time.
     * @param profile Effective sender profile.
     * @param last_frame_bytes Previous complete frame IP bytes.
     */
    void submitted(time_point_t now, const profile_t &profile, std::uint64_t last_frame_bytes) {
      const auto fps_ns = 1000000000ULL / std::max(1, profile.fps);
      const auto wire_ns = std::min<std::uint64_t>(last_frame_bytes, 4000000) * 8000000ULL / std::max(1U, profile.wire_kbps);
      // Bound recovery latency; severe under-capacity is handled by the controller.
      next_ = now + std::chrono::nanoseconds(std::min<std::uint64_t>(1000000000ULL, std::max(fps_ns, wire_ns)));
    }

  private:
    time_point_t next_ {};  ///< Next admission time; first frame is immediate.
  };
}  // namespace video::low_bandwidth
