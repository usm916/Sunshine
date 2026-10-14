<script setup>
import { ref } from 'vue'
import Checkbox from '../../../Checkbox.vue'

const props = defineProps({ config: Object })
const config = ref(props.config)
</script>

<template>
  <Checkbox class="mb-3"
            id="cursor_focus"
            locale-prefix="config"
            v-model="config.cursor_focus"
            default="false"
  />
  <div class="mb-3">
    <label for="cursor_focus_radius" class="form-label">{{ $t('config.cursor_focus_radius') }}</label>
    <input id="cursor_focus_radius" type="number" min="32" max="1024" class="form-control"
           v-model.number="config.cursor_focus_radius" :disabled="config.cursor_focus !== 'enabled'" />
    <div class="form-text">{{ $t('config.cursor_focus_radius_desc') }}</div>
  </div>
  <div class="mb-3">
    <label for="cursor_focus_background_scale" class="form-label">{{ $t('config.cursor_focus_background_scale') }}</label>
    <input id="cursor_focus_background_scale" type="number" min="2" max="16" class="form-control"
           v-model.number="config.cursor_focus_background_scale" :disabled="config.cursor_focus !== 'enabled'" />
    <div class="form-text">{{ $t('config.cursor_focus_background_scale_desc') }}</div>
  </div>
</template>
