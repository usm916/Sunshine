import { mount } from '@vue/test-utils'
import { describe, expect, it } from 'vitest'
import { ref } from 'vue'
import CursorFocus from '../../src_assets/common/assets/web/configs/tabs/audiovideo/CursorFocus.vue'
import AudioVideo from '../../src_assets/common/assets/web/configs/tabs/AudioVideo.vue'
import configTabs from '../../src_assets/common/assets/web/configs/config_tabs.json'

describe('cursor detail preservation', () => {
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
