import { mount } from '@vue/test-utils'
import { describe, expect, it } from 'vitest'
import ViewportCrop from '../../src_assets/common/assets/web/configs/tabs/audiovideo/ViewportCrop.vue'
import configTabs from '../../src_assets/common/assets/web/configs/config_tabs.json'

describe('native pixel viewport crop', () => {
  it('persists the crop toggle and independent edge margins', async () => {
    const config = structuredClone(configTabs.find(tab => tab.id === 'av').options)
    const wrapper = mount(ViewportCrop, { props: { config }, global: { mocks: { $t: key => key } } })
    expect(config.viewport_crop).toBe('disabled')
    expect(config.viewport_crop_deadzone_width).toBe(20)
    expect(config.viewport_crop_deadzone_height).toBe(20)
    expect(config.viewport_crop_monitor_switch).toBe('disabled')
    expect(config.viewport_crop_monitor_switch_delay).toBe(600)
    expect(wrapper.get('#viewport_crop_monitor_switch').element.matches(':disabled')).toBe(true)
    expect(wrapper.get('#viewport_crop_monitor_switch_delay').element.disabled).toBe(true)
    expect(wrapper.get('#viewport_crop_deadzone_width').element.disabled).toBe(true)
    expect(wrapper.get('#viewport_crop_deadzone_height').element.disabled).toBe(true)
    await wrapper.get('#viewport_crop').setValue(true)
    expect(config.viewport_crop).toBe('enabled')
    await wrapper.get('#viewport_crop_monitor_switch').setValue(true)
    expect(config.viewport_crop_monitor_switch).toBe('enabled')
    expect(wrapper.get('#viewport_crop_monitor_switch_delay').element.disabled).toBe(false)
    await wrapper.get('#viewport_crop_monitor_switch_delay').setValue(800)
    expect(config.viewport_crop_monitor_switch_delay).toBe(800)
    await wrapper.get('#viewport_crop_monitor_switch').setValue(false)
    expect(wrapper.get('#viewport_crop_monitor_switch_delay').element.disabled).toBe(true)
    expect(wrapper.get('#viewport_crop_deadzone_width').element.disabled).toBe(false)
    expect(wrapper.get('#viewport_crop_deadzone_height').element.disabled).toBe(false)
    await wrapper.get('#viewport_crop_deadzone_width').setValue(640)
    await wrapper.get('#viewport_crop_deadzone_height').setValue(360)
    expect(config.viewport_crop_deadzone_width).toBe(640)
    expect(config.viewport_crop_deadzone_height).toBe(360)
    await wrapper.get('#viewport_crop').setValue(false)
    expect(config.viewport_crop).toBe('disabled')
    expect(config.viewport_crop_deadzone_width).toBe(640)
    wrapper.unmount()
  })
})
