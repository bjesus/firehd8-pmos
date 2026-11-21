// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>

#define KPD_BASE_PHYS	0x10010000
#define KPD_MEM1	0x0004

struct douglas_keys {
	struct input_dev *input;
	void __iomem *kpd_base;
	struct delayed_work poll_work;
	bool last_volup;
};

static struct douglas_keys *g_dkeys;

static void douglas_keys_poll(struct work_struct *work)
{
	struct douglas_keys *dk = container_of(to_delayed_work(work),
					       struct douglas_keys, poll_work);
	u32 kpd_mem1 = 0xffff;
	bool volup;

	if (dk->kpd_base)
		kpd_mem1 = readl(dk->kpd_base + KPD_MEM1);

	/* Active low (0 = pressed, 1 = released) */
	volup = !(kpd_mem1 & 0x1);

	if (volup != dk->last_volup) {
		input_report_key(dk->input, KEY_VOLUMEUP, volup);
		dk->last_volup = volup;
		input_sync(dk->input);
	}

	schedule_delayed_work(&dk->poll_work, msecs_to_jiffies(25));
}

static int __init douglas_keys_init(void)
{
	struct douglas_keys *dk;
	int ret;

	dk = kzalloc(sizeof(*dk), GFP_KERNEL);
	if (!dk)
		return -ENOMEM;

	dk->kpd_base = ioremap(KPD_BASE_PHYS, 0x100);
	if (!dk->kpd_base) {
		kfree(dk);
		return -ENOMEM;
	}

	dk->input = input_allocate_device();
	if (!dk->input) {
		iounmap(dk->kpd_base);
		kfree(dk);
		return -ENOMEM;
	}

	dk->input->name = "Douglas Volume Up Key";
	dk->input->phys = "douglas-keys/input0";
	dk->input->id.bustype = BUS_HOST;
	dk->input->id.vendor = 0x1949;
	dk->input->id.product = 0x0001;
	dk->input->id.version = 0x0100;

	__set_bit(EV_KEY, dk->input->evbit);
	input_set_capability(dk->input, EV_KEY, KEY_VOLUMEUP);

	ret = input_register_device(dk->input);
	if (ret) {
		input_free_device(dk->input);
		iounmap(dk->kpd_base);
		kfree(dk);
		return ret;
	}

	g_dkeys = dk;
	INIT_DELAYED_WORK(&dk->poll_work, douglas_keys_poll);
	schedule_delayed_work(&dk->poll_work, msecs_to_jiffies(25));

	pr_info("douglas-keys: registered Volume Up (KPD); Power and Volume Down are handled by mtk-pmic-keys\n");
	return 0;
}

static void __exit douglas_keys_exit(void)
{
	if (!g_dkeys)
		return;
	cancel_delayed_work_sync(&g_dkeys->poll_work);
	input_unregister_device(g_dkeys->input);
	iounmap(g_dkeys->kpd_base);
	kfree(g_dkeys);
	g_dkeys = NULL;
}

module_init(douglas_keys_init);
module_exit(douglas_keys_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Amazon Fire HD 8 (douglas) Volume Up key driver");
