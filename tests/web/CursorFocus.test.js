import { mount } from '@vue/test-utils'
import { describe, expect, it } from 'vitest'
import { ref } from 'vue'
import CursorFocus from '../../src_assets/common/assets/web/configs/tabs/audiovideo/CursorFocus.vue'
import AudioVideo from '../../src_assets/common/assets/web/configs/tabs/AudioVideo.vue'
import configTabs from '../../src_assets/common/assets/web/configs/config_tabs.json'

describe('cursor detail preservation', () => {
  it('switches text-following and preserves the movement threshold', async () => {
    const config = structuredClone(configTabs.find(tab => tab.id === 'av').options)
    const wrapper = mount(CursorFocus, { props: { config }, global: { mocks: { $t: key => key } } })
    expect(config.cursor_focus_follow_text).toBe('enabled')
    expect(config.cursor_focus_mouse_move_threshold).toBe(6)
    expect(wrapper.get('#cursor_focus_mouse_move_threshold').element.disabled).toBe(false)
    await wrapper.get('#cursor_focus_mouse_move_threshold').setValue(12)
    expect(config.cursor_focus_mouse_move_threshold).toBe(12)
    await wrapper.get('#cursor_focus_follow_text').setValue(false)
    expect(config.cursor_focus_follow_text).toBe('disabled')
    expect(wrapper.get('#cursor_focus_mouse_move_threshold').element.disabled).toBe(true)
    await wrapper.get('#cursor_focus_follow_text').setValue(true)
    expect(config.cursor_focus_follow_text).toBe('enabled')
    expect(config.cursor_focus_mouse_move_threshold).toBe(12)
    wrapper.unmount()
  })

  it('keeps the feature disabled by default and saves enabled settings', async () => {
    const config = structuredClone(configTabs.find(tab => tab.id === 'av').options)
    const wrapper = mount(CursorFocus, { props: { config }, global: { mocks: { $t: key => key } } })
    expect(config.cursor_focus).toBe('disabled')
    expect(wrapper.get('#cursor_focus_radius').element.disabled).toBe(true)
    expect(wrapper.get('#cursor_focus_background_scale').element.disabled).toBe(true)
    await wrapper.get('#cursor_focus').setValue(true)
    expect(config.cursor_focus).toBe('enabled')
    expect(wrapper.get('#cursor_focus_radius').element.disabled).toBe(false)
    await wrapper.get('#cursor_focus_radius').setValue(96)
    await wrapper.get('#cursor_focus_background_scale').setValue(8)
    expect(config.cursor_focus_radius).toBe(96)
    expect(config.cursor_focus_background_scale).toBe(8)
    await wrapper.get('#cursor_focus').setValue(false)
    expect(config.cursor_focus).toBe('disabled')
    expect(config.cursor_focus_radius).toBe(96)
    wrapper.unmount()
  })

  it('saves rectangle width × height and rolling, frozen and live background modes', async () => {
    const config = structuredClone(configTabs.find(tab => tab.id === 'av').options)
    const wrapper = mount(CursorFocus, { props: { config }, global: { mocks: { $t: key => key } } })
    expect(wrapper.get('#cursor_focus_shape').element.disabled).toBe(true)
    expect(wrapper.get('#cursor_focus_background_mode').element.disabled).toBe(true)
    await wrapper.get('#cursor_focus').setValue(true)
    await wrapper.get('#cursor_focus_shape').setValue('rectangle')
    expect(wrapper.find('#cursor_focus_radius').exists()).toBe(false)
    await wrapper.get('#cursor_focus_width').setValue(640)
    await wrapper.get('#cursor_focus_height').setValue(240)
    expect(config.cursor_focus_width).toBe(640)
    expect(config.cursor_focus_height).toBe(240)
    await wrapper.get('#cursor_focus_background_mode').setValue('rolling')
    expect(config.cursor_focus_background_loop_ms).toBe(1000)
    expect(wrapper.get('#cursor_focus_background_loop_seconds').element.value).toBe('1')
    await wrapper.get('#cursor_focus_background_loop_seconds').setValue(2.5)
    expect(config.cursor_focus_background_loop_ms).toBe(2500)
    await wrapper.get('#cursor_focus_background_mode').setValue('frozen')
    expect(config.cursor_focus_background_loop_ms).toBe(-1)
    expect(wrapper.find('#cursor_focus_background_loop_seconds').exists()).toBe(false)
    await wrapper.get('#cursor_focus_background_mode').setValue('live')
    expect(config.cursor_focus_background_loop_ms).toBe(0)
    await wrapper.get('#cursor_focus_shape').setValue('circle')
    expect(wrapper.find('#cursor_focus_radius').exists()).toBe(true)
    expect(config.cursor_focus_width).toBe(640)
    await wrapper.get('#cursor_focus').setValue(false)
    expect(wrapper.get('#cursor_focus_background_mode').element.disabled).toBe(true)
    wrapper.unmount()
  })

  it('selects automatic presets without erasing manual settings or changing geometry', async () => {
    const config = structuredClone(configTabs.find(tab => tab.id === 'av').options)
    const wrapper = mount(CursorFocus, { props: { config }, global: { mocks: { $t: key => key } } })
    expect(config.cursor_focus_auto).toBe('disabled')
    await wrapper.get('#cursor_focus').setValue(true)
    await wrapper.get('#cursor_focus_shape').setValue('rectangle')
    await wrapper.get('#cursor_focus_background_mode').setValue('rolling')
    await wrapper.get('#cursor_focus_auto').setValue(true)
    expect(config.cursor_focus_auto).toBe('enabled')
    expect(wrapper.get('#cursor_focus_background_scale').element.disabled).toBe(true)
    expect(wrapper.get('#cursor_focus_background_mode').element.disabled).toBe(true)
    expect(wrapper.get('#cursor_focus_background_loop_seconds').element.disabled).toBe(true)
    expect(wrapper.get('#cursor_focus_width').element.disabled).toBe(false)
    expect(wrapper.get('#cursor_focus_height').element.disabled).toBe(false)
    expect(config.cursor_focus_background_loop_ms).toBe(1000)
    expect(config.cursor_focus_background_scale).toBe(4)
    await wrapper.get('#cursor_focus_auto').setValue(false)
    expect(wrapper.get('#cursor_focus_background_scale').element.disabled).toBe(false)
    expect(wrapper.get('#cursor_focus_background_mode').element.disabled).toBe(false)
    expect(wrapper.get('#cursor_focus_background_loop_seconds').element.disabled).toBe(false)
    wrapper.unmount()
  })

  it.each(['windows', 'linux', 'freebsd', 'macos'])('shows controls only on a Windows host: %s', platform => {
    const config = structuredClone(configTabs.find(tab => tab.id === 'av').options)
    const wrapper = mount(AudioVideo, {
      props: { config, platform },
      global: {
        mocks: { $t: key => key },
        provide: { platform: ref(platform), i18n: { t: key => key } },
        stubs: { AdapterNameSelector: true, DisplayOutputSelector: true, DisplayDeviceOptions: true, DisplayModesSettings: true },
      },
    })
    expect(wrapper.find('#cursor_focus').exists()).toBe(platform === 'windows')
    wrapper.unmount()
  })
})
