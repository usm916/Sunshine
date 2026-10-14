<script setup>
import { computed, ref } from 'vue'
import Checkbox from '../../../Checkbox.vue'

const props = defineProps({ config: Object })
const config = ref(props.config)
const refreshMode = computed({
  get: () => config.value.cursor_focus_background_loop_ms < 0 ? 'frozen'
    : config.value.cursor_focus_background_loop_ms > 0 ? 'rolling' : 'live',
  set: mode => { config.value.cursor_focus_background_loop_ms = mode === 'frozen' ? -1 : mode === 'rolling' ? 1000 : 0 },
})
const loopSeconds = computed({
  get: () => config.value.cursor_focus_background_loop_ms / 1000,
  set: value => { config.value.cursor_focus_background_loop_ms = Math.max(1, Math.round(Number(value) * 1000)) },
})
</script>

<template>
  <Checkbox class="mb-3"
            id="cursor_focus"
            locale-prefix="config"
            v-model="config.cursor_focus"
            default="false"
  />
  <Checkbox class="mb-3"
            id="cursor_focus_auto"
            locale-prefix="config"
            v-model="config.cursor_focus_auto"
            default="false"
  />
  <div class="mb-3">
    <label for="cursor_focus_shape" class="form-label">{{ $t('config.cursor_focus_shape') }}</label>
    <select id="cursor_focus_shape" class="form-select" v-model="config.cursor_focus_shape"
            :disabled="config.cursor_focus !== 'enabled'">
      <option value="circle">{{ $t('config.cursor_focus_shape_circle') }}</option>
      <option value="rectangle">{{ $t('config.cursor_focus_shape_rectangle') }}</option>
    </select>
  </div>
  <div v-if="config.cursor_focus_shape !== 'rectangle'" class="mb-3">
    <label for="cursor_focus_radius" class="form-label">{{ $t('config.cursor_focus_radius') }}</label>
    <input id="cursor_focus_radius" type="number" min="32" max="1024" class="form-control"
           v-model.number="config.cursor_focus_radius" :disabled="config.cursor_focus !== 'enabled'" />
    <div class="form-text">{{ $t('config.cursor_focus_radius_desc') }}</div>
  </div>
  <div v-else class="mb-3">
    <div class="row align-items-end">
      <div class="col">
        <label for="cursor_focus_width" class="form-label">{{ $t('config.cursor_focus_width') }}</label>
        <input id="cursor_focus_width" type="number" min="32" max="8192" class="form-control"
               v-model.number="config.cursor_focus_width" :disabled="config.cursor_focus !== 'enabled'" />
      </div>
      <div class="col-auto pb-2">×</div>
      <div class="col">
        <label for="cursor_focus_height" class="form-label">{{ $t('config.cursor_focus_height') }}</label>
        <input id="cursor_focus_height" type="number" min="32" max="8192" class="form-control"
               v-model.number="config.cursor_focus_height" :disabled="config.cursor_focus !== 'enabled'" />
      </div>
    </div>
    <div class="form-text">{{ $t('config.cursor_focus_rectangle_desc') }}</div>
  </div>
  <div class="mb-3">
    <label for="cursor_focus_background_scale" class="form-label">{{ $t('config.cursor_focus_background_scale') }}</label>
    <input id="cursor_focus_background_scale" type="number" min="2" max="16" class="form-control"
           v-model.number="config.cursor_focus_background_scale" :disabled="config.cursor_focus !== 'enabled' || config.cursor_focus_auto === 'enabled'" />
    <div class="form-text">{{ $t('config.cursor_focus_background_scale_desc') }}</div>
  </div>
  <div class="mb-3">
    <label for="cursor_focus_background_mode" class="form-label">{{ $t('config.cursor_focus_background_mode') }}</label>
    <select id="cursor_focus_background_mode" class="form-select" v-model="refreshMode"
            :disabled="config.cursor_focus !== 'enabled' || config.cursor_focus_auto === 'enabled'">
      <option value="live">{{ $t('config.cursor_focus_background_mode_live') }}</option>
      <option value="rolling">{{ $t('config.cursor_focus_background_mode_rolling') }}</option>
      <option value="frozen">{{ $t('config.cursor_focus_background_mode_frozen') }}</option>
    </select>
    <div class="form-text">{{ $t('config.cursor_focus_background_mode_desc') }}</div>
  </div>
  <div v-if="refreshMode === 'rolling'" class="mb-3">
    <label for="cursor_focus_background_loop_seconds" class="form-label">{{ $t('config.cursor_focus_background_loop_seconds') }}</label>
    <input id="cursor_focus_background_loop_seconds" type="number" min="0.001" max="60" step="0.001"
           class="form-control" v-model="loopSeconds" :disabled="config.cursor_focus !== 'enabled' || config.cursor_focus_auto === 'enabled'" />
    <div class="form-text">{{ $t('config.cursor_focus_background_loop_seconds_desc') }}</div>
  </div>
</template>
