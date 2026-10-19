<script setup>
import Checkbox from '../../../Checkbox.vue'
defineProps({ config: Object })
</script>

<template>
  <Checkbox class="mb-3" id="viewport_crop" locale-prefix="config"
            v-model="config.viewport_crop" default="false" />
  <div class="mb-3">
    <div class="row">
      <div class="col">
        <label for="viewport_crop_deadzone_width" class="form-label">{{ $t('config.viewport_crop_deadzone_width') }}</label>
        <input id="viewport_crop_deadzone_width" type="number" min="0" max="16384" class="form-control"
               v-model.number="config.viewport_crop_deadzone_width" :disabled="config.viewport_crop !== 'enabled'" />
      </div>
      <div class="col">
        <label for="viewport_crop_deadzone_height" class="form-label">{{ $t('config.viewport_crop_deadzone_height') }}</label>
        <input id="viewport_crop_deadzone_height" type="number" min="0" max="16384" class="form-control"
               v-model.number="config.viewport_crop_deadzone_height" :disabled="config.viewport_crop !== 'enabled'" />
      </div>
    </div>
    <div class="form-text">{{ $t('config.viewport_crop_deadzone_desc') }}</div>
  </div>
  <fieldset :disabled="config.viewport_crop !== 'enabled'">
  <Checkbox class="mb-3" id="viewport_crop_monitor_switch" locale-prefix="config"
            v-model="config.viewport_crop_monitor_switch" default="false" />
  <div class="mb-3">
    <label for="viewport_crop_monitor_switch_delay" class="form-label">{{ $t('config.viewport_crop_monitor_switch_delay') }}</label>
    <input id="viewport_crop_monitor_switch_delay" type="number" min="100" max="5000" step="100" class="form-control"
           v-model.number="config.viewport_crop_monitor_switch_delay"
           :disabled="config.viewport_crop !== 'enabled' || config.viewport_crop_monitor_switch !== 'enabled'" />
  </div>
  </fieldset>
</template>
