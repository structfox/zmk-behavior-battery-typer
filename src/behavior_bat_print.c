#define DT_DRV_COMPAT zmk_behavior_bat_print

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/battery.h>
#include <zmk/events/keycode_state_changed.h>
#include <dt-bindings/zmk/keys.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) && IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
#include <zmk/split/central.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/*
 * structfox fork — Charybdis 동글 구조(central=동글, USB라 자기 배터리 무의미)에 맞춤.
 * 동글 배터리는 건너뛰고, 좌/우 peripheral(0,1)을 라벨 붙여 "L<pct> R<pct>" 로 타이핑한다.
 * peripheral index ↔ 좌/우 매핑은 BLE 연결/슬롯에 따르므로, 플래시 후 실제 출력이
 * 좌우 반대면 아래 labels[] 순서만 바꾸면 된다.
 */

#define BAT_PRINT_MAX_SEQ 16

static const uint32_t digit_keycodes[] = {N0, N1, N2, N3, N4, N5, N6, N7, N8, N9};

static uint32_t bat_print_seq[BAT_PRINT_MAX_SEQ];
static uint8_t bat_print_seq_len;

static void seq_append(uint32_t keycode)
{
	if (bat_print_seq_len < BAT_PRINT_MAX_SEQ) {
		bat_print_seq[bat_print_seq_len++] = keycode;
	}
}

static void seq_append_pct(uint8_t pct)
{
	if (pct > 100) {
		pct = 100;
	}
	if (pct >= 100) {
		seq_append(N1);
		seq_append(N0);
		seq_append(N0);
	} else if (pct >= 10) {
		seq_append(digit_keycodes[pct / 10]);
		seq_append(digit_keycodes[pct % 10]);
	} else {
		seq_append(digit_keycodes[pct]);
	}
}

static void bat_print_work_handler(struct k_work *work)
{
	int64_t ts;

	for (uint8_t i = 0; i < bat_print_seq_len; i++) {
		ts = k_uptime_get();
		raise_zmk_keycode_state_changed_from_encoded(bat_print_seq[i], true, ts);
		k_msleep(20);
		ts = k_uptime_get();
		raise_zmk_keycode_state_changed_from_encoded(bat_print_seq[i], false, ts);
		k_msleep(10);
	}
}

K_WORK_DEFINE(bat_print_work, bat_print_work_handler);

static int on_bat_print_binding_pressed(struct zmk_behavior_binding *binding,
					struct zmk_behavior_binding_event event)
{
	bat_print_seq_len = 0;

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) && IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
	/* 동글(central) 자체 배터리는 USB라 무의미 → 스킵하고 좌/우 peripheral 만 출력. */
	static const uint32_t labels[] = {LS(L), LS(R)};
	uint8_t periph_pct;

	for (uint8_t i = 0; i < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS && i < 2; i++) {
		if (zmk_split_central_get_peripheral_battery_level(i, &periph_pct) == 0) {
			if (bat_print_seq_len > 0) {
				seq_append(SPACE);
			}
			seq_append(labels[i]);
			seq_append_pct(periph_pct);
		}
	}
#else
	/* split 미사용/central 아님: 로컬 배터리만 숫자로 */
	seq_append_pct(zmk_battery_state_of_charge());
#endif

	k_work_submit(&bat_print_work);
	return ZMK_BEHAVIOR_OPAQUE;
}

static int on_bat_print_binding_released(struct zmk_behavior_binding *binding,
					 struct zmk_behavior_binding_event event)
{
	return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_bat_print_driver_api = {
	.binding_pressed = on_bat_print_binding_pressed,
	.binding_released = on_bat_print_binding_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
			CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
			&behavior_bat_print_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
