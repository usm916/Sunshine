import { mount } from '@vue/test-utils'
import { describe, expect, it } from 'vitest'
import LowBandwidth from '../../src_assets/common/assets/web/configs/tabs/audiovideo/LowBandwidth.vue'
import configTabs from '../../src_assets/common/assets/web/configs/config_tabs.json'

describe('low bandwidth sessions', () => {
  it('defaults to adaptive and persists disabled, fixed and adaptive controls', async () => {
    const config = structuredClone(configTabs.find(tab => tab.id === 'av').options)
    const wrapper = mount(LowBandwidth, { props: { config }, global: { mocks: { $t: key => key } } })
    expect(config.low_bandwidth_mode).toBe('adaptive')
    expect(config.low_bandwidth_target_kbps).toBe(0)
    expect(wrapper.get('#low_bandwidth_target_kbps').attributes('min')).toBe('0')
    expect(wrapper.get('#low_bandwidth_target_kbps').element.disabled).toBe(false)
    await wrapper.get('#low_bandwidth_mode').setValue('disabled')
    expect(wrapper.get('#low_bandwidth_target_kbps').element.disabled).toBe(true)
    await wrapper.get('#low_bandwidth_mode').setValue('fixed')
    expect(wrapper.get('#low_bandwidth_target_kbps').element.disabled).toBe(false)
    await wrapper.get('#low_bandwidth_target_kbps').setValue(250)
    await wrapper.get('#low_bandwidth_maximum_fps').setValue(5)
    expect(config.low_bandwidth_target_kbps).toBe(250)
    expect(config.low_bandwidth_maximum_fps).toBe(5)
    await wrapper.get('#low_bandwidth_mode').setValue('adaptive')
    await wrapper.get('#low_bandwidth_minimum_kbps').setValue(64)
    await wrapper.get('#low_bandwidth_minimum_fps').setValue(1)
    expect(config.low_bandwidth_minimum_kbps).toBe(64)
    expect(config.low_bandwidth_minimum_fps).toBe(1)
    await wrapper.get('#low_bandwidth_mode').setValue('disabled')
    expect(wrapper.get('#low_bandwidth_minimum_fps').element.disabled).toBe(true)
  })
})
