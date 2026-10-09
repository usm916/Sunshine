/**
 * @file tests/unit/test_clipboard_session.cpp
 * @brief Verify ownership, retries, limits, and disconnect isolation.
 */
#include "../tests_common.h"
#include "src/platform/clipboard_binding.h"
#include "src/platform/clipboard_policy.h"
#include "src/platform/clipboard_session.h"

TEST(ClipboardBinding, DisconnectRejectsLateInputAndRebindReplacesMailbox) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto first = mail->queue<platf::clipboard_text_t>("first");
  auto second = mail->queue<platf::clipboard_text_t>("second");
  platf::clipboard_binding_t binding(first);
  int actions = 0;
  auto action = [&](const auto &queue) {
    EXPECT_EQ(queue, first);
    ++actions;
  };
  EXPECT_TRUE(binding.apply(action));
  binding.stop([&](const auto &queue) {
    EXPECT_EQ(queue, first);
  });
  EXPECT_FALSE(binding.apply(action));
  binding.rebind(second, [&](const auto &queue) {
    EXPECT_EQ(queue, first);
  });
  EXPECT_TRUE(binding.apply([&](const auto &queue) {
    EXPECT_EQ(queue, second);
    ++actions;
  }));
  EXPECT_EQ(actions, 2);
  platf::clipboard_binding_t empty({});
  EXPECT_FALSE(empty.apply(action));
}

TEST(ClipboardSession, SingleOwnerAndDisconnectDiscardPendingData) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto first = mail->queue<platf::clipboard_text_t>("first");
  auto second = mail->queue<platf::clipboard_text_t>("second");
  platf::clipboard_session_t state;
  EXPECT_FALSE(state.subscribe({}));
  EXPECT_TRUE(state.subscribe(first));
  EXPECT_TRUE(state.subscribe(first));
  EXPECT_FALSE(state.subscribe(second));
  EXPECT_FALSE(state.stage(second, "private"));
  EXPECT_TRUE(state.stage(first, "pending"));
  state.unsubscribe(second);
  EXPECT_TRUE(state.pending);
  state.unsubscribe(first);
  EXPECT_FALSE(state.pending);
  EXPECT_FALSE(state.owner);
  EXPECT_TRUE(state.subscribe(second));
  EXPECT_TRUE(state.publish("snapshot"));
  EXPECT_FALSE(first->peek());
  EXPECT_EQ(second->pop()->text, "snapshot");
}

TEST(ClipboardSession, FailedWritesRetryAndSuccessfulWritesDoNotEcho) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto queue = mail->queue<platf::clipboard_text_t>("clipboard");
  platf::clipboard_session_t state;
  EXPECT_FALSE(state.stage(queue, "text"));
  EXPECT_FALSE(state.publish("text"));
  state.subscribe(queue);
  EXPECT_TRUE(state.stage(queue, "text"));
  state.applied(false);
  EXPECT_TRUE(state.pending);
  EXPECT_FALSE(state.has_snapshot);
  EXPECT_TRUE(state.stage(queue, "latest"));
  state.applied(true);
  EXPECT_FALSE(state.pending);
  EXPECT_FALSE(state.publish("latest"));
  EXPECT_FALSE(queue->peek());
  EXPECT_TRUE(state.publish("local"));
  EXPECT_FALSE(state.publish("local"));
  EXPECT_EQ(queue->pop()->text, "local");
  state.applied(true);
}

TEST(ClipboardSession, RejectInvalidAndOversizeTextWithoutTruncating) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto queue = mail->queue<platf::clipboard_text_t>("clipboard");
  platf::clipboard_session_t state;
  state.subscribe(queue);
  const std::string invalid("a\0b", 3);
  EXPECT_FALSE(state.stage(queue, invalid));
  EXPECT_FALSE(state.publish(invalid));
  EXPECT_FALSE(state.stage(queue, ""));
  EXPECT_FALSE(state.publish(""));
  const std::string oversize(SS_CLIPBOARD_TEXT_MAX + 1, 'a');
  EXPECT_FALSE(state.stage(queue, oversize));
  EXPECT_FALSE(state.publish(oversize));
  const std::string maximum(SS_CLIPBOARD_TEXT_MAX, 'a');
  EXPECT_TRUE(state.stage(queue, maximum));
  EXPECT_TRUE(state.publish(maximum));
  EXPECT_EQ(queue->pop()->text.size(), SS_CLIPBOARD_TEXT_MAX);
  queue->stop();
  EXPECT_FALSE(state.publish("cannot queue"));
}

TEST(ClipboardPolicy, HostDirections) {
  const auto saved = config::input.clipboard_mode;
  config::input.clipboard_mode = "disabled";
  EXPECT_EQ(platf::clipboard_permissions(), 0);
#ifdef _WIN32
  config::input.clipboard_mode = "host_to_client";
  EXPECT_EQ(platf::clipboard_permissions(), SS_CLIPBOARD_HOST_TO_CLIENT);
  config::input.clipboard_mode = "client_to_host";
  EXPECT_EQ(platf::clipboard_permissions(), SS_CLIPBOARD_CLIENT_TO_HOST);
  config::input.clipboard_mode = "bidirectional";
  EXPECT_EQ(platf::clipboard_permissions(), 3);
#endif
  config::input.clipboard_mode = "invalid";
  EXPECT_EQ(platf::clipboard_permissions(), 0);
  config::input.clipboard_mode = saved;
}
