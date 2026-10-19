<script setup>
import { computed } from 'vue'
const props = defineProps({ config: Object })
const disabled = computed(() => props.config.low_bandwidth_mode === 'disabled')
const fields = [
  ['low_bandwidth_target_kbps', 0, 1000000],
  ['low_bandwidth_minimum_kbps', 32, 1000000],
  ['low_bandwidth_maximum_fps', 1, 120],
  ['low_bandwidth_minimum_fps', 1, 120],
]
</script>
<template>
  <div class="mb-4">
    <label for="low_bandwidth_mode" class="form-label">{{ $t('config.low_bandwidth_mode') }}</label>
    <select id="low_bandwidth_mode" class="form-select" v-model="config.low_bandwidth_mode">
      <option value="disabled">{{ $t('config.low_bandwidth_disabled') }}</option>
      <option value="fixed">{{ $t('config.low_bandwidth_fixed') }}</option>
      <option value="adaptive">{{ $t('config.low_bandwidth_adaptive') }}</option>
    </select>
    <div class="form-text">{{ $t('config.low_bandwidth_mode_desc') }}</div>
    <div v-for="[key, minimum, maximum] in fields" :key="key" class="mt-3">
      <label :for="key" class="form-label">{{ $t('config.' + key) }}</label>
      <input :id="key" type="number" class="form-control" :min="minimum" :max="maximum"
             v-model.number="config[key]" :disabled="disabled" />
    </div>
  </div>
</template>
