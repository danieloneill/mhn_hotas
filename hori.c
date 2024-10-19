// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Hori/Namco Flightstick
 *
 * Copyright (C) 2024 Daniel O'Neill <daniel@oneill.app>
 * Copyright (C) 2024 Tamanegi <tamanegi.org>
 * Copyright (C) 2018 Marcus Folkesson <marcus.folkesson@gmail.com>
 */

#include <linux/cleanup.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <linux/usb.h>
#include <linux/usb/input.h>

#define HORI_VENDOR_ID		0x06d3
#define HORI_PRODUCT_ID		0x0f10

#define HORI_POLL_VR0		0x00
#define HORI_POLL_VR1		0x01

/* input: vendor request 0x00 */
struct hori_raw_input_vr_00 {
    u8 fire_c : 1;           /* button fire-c */
    u8 button_d : 1;         /* button D */
    u8 hat : 1;              /* hat press */
    u8 button_st : 1;        /* button ST */

    u8 dpad1_top : 1;        /* d-pad 1 top */
    u8 dpad1_right : 1;      /* d-pad 1 right */
    u8 dpad1_bottom : 1;     /* d-pad 1 bottom */
    u8 dpad1_left : 1;       /* d-pad 1 left */

    u8 reserved1 : 1;
    u8 reserved2 : 1;
    u8 reserved3 : 1;
    u8 reserved4 : 1;

    u8 reserved5 : 1;
    u8 launch : 1;           /* button lauch */
    u8 trigger : 1;          /* trigger */
    u8 reserved6 : 1;
};

/* input: vendor request 0x01 */
struct hori_raw_input_vr_01 {
    u8 reserved1 : 1;
    u8 reserved2 : 1;
    u8 reserved3 : 1;
    u8 reserved4 : 1;

    u8 dpad3_right : 1;      /* d-pad 3 right */
    u8 dpad3_middle : 1;     /* d-pad 3 middle */
    u8 dpad3_left : 1;       /* d-pad 3 left */
    u8 reserved5 : 1;

    u8 mode_select : 2;      /* mode select (M1 - M2 - M3, 2 - 1 - 3) */
    u8 reserved6 : 1;
    u8 button_sw1 : 1;       /* button sw-1 */

    u8 dpad2_top : 1;        /* d-pad 2 top */
    u8 dpad2_right : 1;      /* d-pad 2 right */
    u8 dpad2_bottom : 1;     /* d-pad 2 bottom */
    u8 dpad2_left : 1;       /* d-pad 2 left */
};

struct hori {
	struct input_dev	*input;
	struct usb_interface	*intf;
	struct usb_endpoint_descriptor *epirq;
	struct urb		*urb, *urb_ctl;
	struct mutex		pm_mutex;
	bool			is_open;
	char			phys[64];
	struct usb_ctrlrequest	*ctl_req;
	struct hori_raw_input_vr_00 vr0;
	struct hori_raw_input_vr_01 vr1;
};

#define ERRCASE(CODE) case -CODE: strcpy(errcode, #CODE);break;
static void hori_urb_error(const struct device *dev, int error)
{
	char errcode[16];
	switch(error) {
		ERRCASE(ENOMEM)
		ERRCASE(EBUSY)
		ERRCASE(ENODEV)
		ERRCASE(ENOENT)
		ERRCASE(ENXIO)
		ERRCASE(EINVAL)
		ERRCASE(EXDEV)
		ERRCASE(EFBIG)
		ERRCASE(EPIPE)
		ERRCASE(EMSGSIZE)
		ERRCASE(ENOSPC)
		ERRCASE(ESHUTDOWN)
		ERRCASE(EPERM)
		ERRCASE(EHOSTUNREACH)
		ERRCASE(ENOEXEC)
		ERRCASE(EBADR)
		default:
			strcpy(errcode, "Unknown");
	}
	dev_err(dev,
		"%s - usb_submit_urb failed with result: %d (%s)",
		__func__, error, errcode);
}

static void hori_poll_vr0(struct hori *hori);
static void hori_poll_vr1(struct hori *hori);

static void hori_poll_vr0_complete(struct urb *urb)
{
	struct hori *hori = urb->context;

	//printk(KERN_INFO "hori_poll_vr0_complete: %d\n", urb->status);
	switch (urb->status) {
	case 0:
		break;
	case -ETIME:
		// this urb is timing out
		dev_warn(&hori->intf->dev,
			"%s - urb timed out - was the device unplugged?\n",
			__func__);
		return;
	case -EPIPE:
		// stalled
		goto exit2;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		// this urb is terminated, clean up
		dev_warn(&hori->intf->dev, "%s - urb shutting down with status: %d\n",
			__func__, urb->status);
		return;
	default:
		dev_err(&hori->intf->dev, "%s - nonzero urb status received: %d\n",
			__func__, urb->status);
		goto exit2;
	}

	input_report_key(hori->input, BTN_TRIGGER_HAPPY1, !hori->vr0.fire_c);
	input_report_key(hori->input, BTN_TRIGGER_HAPPY2, !hori->vr0.button_d);
	input_report_key(hori->input, BTN_TRIGGER_HAPPY3, !hori->vr0.hat);
	input_report_key(hori->input, BTN_TRIGGER_HAPPY4, !hori->vr0.button_st);

	input_report_key(hori->input, BTN_TRIGGER_HAPPY5, !hori->vr0.dpad1_top);
	input_report_key(hori->input, BTN_TRIGGER_HAPPY6, !hori->vr0.dpad1_right);
	input_report_key(hori->input, BTN_TRIGGER_HAPPY7, !hori->vr0.dpad1_bottom);
	input_report_key(hori->input, BTN_TRIGGER_HAPPY8, !hori->vr0.dpad1_left);
/*
	input_report_abs(hori->input, ABS_TILT_X, !hori->vr0.dpad1_left ? 0 : !hori->vr0.dpad1_right ? 2 : 1);
	input_report_abs(hori->input, ABS_TILT_Y, !hori->vr0.dpad1_top ? 0 : !hori->vr0.dpad1_bottom ? 2 : 1);
*/
	input_report_key(hori->input, BTN_THUMB, !hori->vr0.launch);
	input_report_key(hori->input, BTN_TRIGGER, !hori->vr0.trigger);

	/*
	printk(KERN_INFO "hori: data: 1:%x 2:%x 3:%x 4:%x 5:%x 6:%x\n",
		hori->vr0.reserved1, hori->vr0.reserved2, hori->vr0.reserved3,
		hori->vr0.reserved4, hori->vr0.reserved5, hori->vr0.reserved6);
		*/
/*
	dev_info(&hori->intf->dev, "%u %u %u %u %u %u %u %u %u %u\n",
			hori->vr0.fire_c, hori->vr0.button_d, hori->vr0.hat, hori->vr0.button_st,
			hori->vr0.dpad1_top, hori->vr0.dpad1_right, hori->vr0.dpad1_bottom, hori->vr0.dpad1_left,
			hori->vr0.launch, hori->vr0.trigger);
*/
	input_sync(hori->input);

exit2:
	hori_poll_vr1(hori);
}

static void hori_poll_vr0(struct hori *hori)
{
	struct usb_device *udev = interface_to_usbdev(hori->intf);

	hori->ctl_req->bRequest = HORI_POLL_VR0;
        hori->ctl_req->wLength = sizeof(hori->vr0);

	usb_fill_control_urb( hori->urb_ctl, udev,
			usb_rcvctrlpipe(udev, 0),
			(unsigned char *)hori->ctl_req,
			&hori->vr0, sizeof(hori->vr0),
			hori_poll_vr0_complete, hori);
/*
	dev_warn(&hori->intf->dev,
		"%s - usb_submit_urb\n",
		__func__);
		*/
	int error = usb_submit_urb(hori->urb_ctl, GFP_KERNEL);
	if (error && error != -EPERM)
		hori_urb_error(&hori->intf->dev, error);
}

static void hori_poll_vr1_complete(struct urb *urb)
{
	struct hori *hori = urb->context;

	//printk(KERN_INFO "hori_poll_vr1_complete: %d\n", urb->status);
	switch (urb->status) {
	case 0:
		break;
	case -ETIME:
		// this urb is timing out
		dev_warn(&hori->intf->dev,
			"%s - urb timed out - was the device unplugged?\n",
			__func__);
		return;
	case -EPIPE:
		// stalled
		goto exit3;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		// this urb is terminated, clean up
		dev_warn(&hori->intf->dev, "%s - urb shutting down with status: %d\n",
			__func__, urb->status);
		return;
	default:
		dev_err(&hori->intf->dev, "%s - nonzero urb status received: %d\n",
			__func__, urb->status);
		goto exit3;
	}
/*
	printk(KERN_INFO "hori: data: 1:%x 2:%x 3:%x 4:%x 5:%x 6:%x\n",
		hori->vr1.reserved1, hori->vr1.reserved2, hori->vr1.reserved3,
		hori->vr1.reserved4, hori->vr1.reserved5, hori->vr1.reserved6);
*/
	input_report_key(hori->input, BTN_THUMB2, !hori->vr1.dpad3_right);
	input_report_key(hori->input, BTN_C, !hori->vr1.dpad3_middle);
	input_report_key(hori->input, BTN_X, !hori->vr1.dpad3_left);
	input_report_key(hori->input, BTN_Y, !hori->vr1.button_sw1);

/*
	// TODO: mode_select
	input_report_key(hori->input, BTN_BASE, hori->vr1.mode_select == 1);
	input_report_key(hori->input, BTN_GEAR_UP, hori->vr1.mode_select == 2);
	input_report_key(hori->input, BTN_GEAR_DOWN, hori->vr1.mode_select == 3);
*/

	input_report_abs(hori->input, ABS_Z, !hori->vr1.dpad2_left ? 0 : !hori->vr1.dpad2_right ? 2 : 1);
	input_report_abs(hori->input, ABS_RZ, !hori->vr1.dpad2_top ? 0 : !hori->vr1.dpad2_bottom ? 2 : 1);
	/*
	input_report_key(hori->input, BTN_Z, !hori->vr1.dpad2_bottom);
	input_report_key(hori->input, BTN_TL, !hori->vr1.dpad2_left);
	input_report_key(hori->input, BTN_TR, !hori->vr1.dpad2_top);
	input_report_key(hori->input, BTN_MODE, !hori->vr1.dpad2_right);
*/
	input_sync(hori->input);

exit3:
	hori_poll_vr0(hori);
}

static void hori_poll_vr1(struct hori *hori)
{
	struct usb_device *udev = interface_to_usbdev(hori->intf);

	hori->ctl_req->bRequest = HORI_POLL_VR1;
        hori->ctl_req->wLength = sizeof(hori->vr1);

	usb_fill_control_urb( hori->urb_ctl, udev,
			usb_rcvctrlpipe(udev, 0),
			(unsigned char *)hori->ctl_req,
			&hori->vr1, sizeof(hori->vr1),
			hori_poll_vr1_complete, hori);
/*
	dev_warn(&hori->intf->dev,
		"%s - usb_submit_urb\n",
		__func__);
		*/
	int error = usb_submit_urb(hori->urb_ctl, GFP_KERNEL);
	if (error && error != -EPERM)
		hori_urb_error(&hori->intf->dev, error);
}

static void hori_usb_irq(struct urb *urb)
{
	struct hori *hori = urb->context;
	u8 *data = urb->transfer_buffer;
	int error;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ETIME:
		/* this urb is timing out */
		dev_dbg(&hori->intf->dev,
			"%s - urb timed out - was the device unplugged?\n",
			__func__);
		return;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -EPIPE:
		/* this urb is terminated, clean up */
		dev_dbg(&hori->intf->dev, "%s - urb shutting down with status: %d\n",
			__func__, urb->status);
		return;
	default:
		dev_dbg(&hori->intf->dev, "%s - nonzero urb status received: %d\n",
			__func__, urb->status);
		goto exit;
	}

	if (urb->actual_length == 8) {
		input_report_abs(hori->input, ABS_X, data[0]);
		input_report_abs(hori->input, ABS_Y, data[1]);
		input_report_abs(hori->input, ABS_RUDDER, data[2]);
		input_report_abs(hori->input, ABS_RX, data[3]);
		input_report_abs(hori->input, ABS_RY, data[4]);
		input_report_abs(hori->input, ABS_THROTTLE, data[5]);

		input_report_key(hori->input, BTN_A, data[6] < 0xc0);
		input_report_key(hori->input, BTN_B, data[7] < 0xc0);

		//printk(KERN_INFO "hori: data: %x %x\n", data[6], data[7]);

		input_sync(hori->input);
	} else {
		dev_warn(&hori->intf->dev,
			"%s - urb->actual_length == %d\n",
			__func__, urb->actual_length);
	}

exit:
	/* Resubmit to fetch new fresh URBs */
	error = usb_submit_urb(urb, GFP_ATOMIC);
	if (error && error != -EPERM)
		hori_urb_error(&hori->intf->dev, error);
}

static int hori_open(struct input_dev *input)
{
	struct hori *hori = input_get_drvdata(input);
	int error;

	guard(mutex)(&hori->pm_mutex);
	error = usb_submit_urb(hori->urb, GFP_KERNEL);
	if (error) {
		dev_err(&hori->intf->dev,
			"%s - usb_submit_urb failed, error: %d\n",
			__func__, error);
		return -EIO;
	}

	hori->is_open = true;
	hori_poll_vr0(hori);

	return 0;
}

static void hori_close(struct input_dev *input)
{
	struct hori *hori = input_get_drvdata(input);

	dev_warn(&hori->intf->dev,
		"%s - usb_kill_urb\n",
		__func__);
	guard(mutex)(&hori->pm_mutex);
	usb_kill_urb(hori->urb);
	usb_kill_urb(hori->urb_ctl);
	hori->is_open = false;
}

static void hori_free_urb(void *_hori)
{
	struct hori *hori = _hori;

	dev_warn(&hori->intf->dev,
		"%s - usb_free_urb\n",
		__func__);
	usb_free_urb(hori->urb);
	usb_free_urb(hori->urb_ctl);
}


static int hori_probe(struct usb_interface *intf,
		      const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct hori *hori;
	struct usb_endpoint_descriptor *epirq;
	size_t xfer_size;
	void *xfer_buf;
	int error;

	/*
	 * Locate the endpoint information.
	 */
	error = usb_find_common_endpoints(intf->cur_altsetting,
					  NULL, NULL, &epirq, NULL);
	if (error) {
		dev_err(&intf->dev, "Could not find endpoint\n");
		return error;
	}

	hori = devm_kzalloc(&intf->dev, sizeof(*hori), GFP_KERNEL);
	if (!hori)
		return -ENOMEM;

	mutex_init(&hori->pm_mutex);
	hori->intf = intf;
	hori->epirq = epirq;

	usb_set_intfdata(hori->intf, hori);

	xfer_size = usb_endpoint_maxp(epirq);
	xfer_buf = devm_kmalloc(&intf->dev, xfer_size, GFP_KERNEL);
	if (!xfer_buf)
		return -ENOMEM;

	hori->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!hori->urb)
		return -ENOMEM;

	hori->ctl_req = devm_kzalloc(&intf->dev, sizeof(struct usb_ctrlrequest), GFP_KERNEL);
	if (!hori->ctl_req)
		return -ENOMEM;

	//hori->ctl_req->bRequest = USB_REQ_GET_STATUS;
	hori->ctl_req->bRequestType = USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT;
	hori->ctl_req->wValue = 0;
        hori->ctl_req->wIndex = 1;

	hori->ctl_req->bRequest = HORI_POLL_VR0;
        hori->ctl_req->wLength = sizeof(hori->vr0);

	hori->urb_ctl = usb_alloc_urb(0, GFP_KERNEL);
	if (!hori->urb_ctl)
		return -ENOMEM;

	error = devm_add_action_or_reset(&intf->dev, hori_free_urb, hori);
	if (error)
		return error;

	usb_fill_int_urb(hori->urb, udev,
			 usb_rcvintpipe(udev, epirq->bEndpointAddress),
			 xfer_buf, xfer_size, hori_usb_irq, hori, epirq->bInterval); // TODO: maybe 1 instead of bInterval?

	hori->input = devm_input_allocate_device(&intf->dev);
	if (!hori->input) {
		dev_err(&intf->dev, "couldn't allocate input device\n");
		return -ENOMEM;
	}

	hori->input->name = "Mitsubishi Hori/Namco Flightstick";

	usb_make_path(udev, hori->phys, sizeof(hori->phys));
	strlcat(hori->phys, "/input0", sizeof(hori->phys));
	hori->input->phys = hori->phys;

	usb_to_input_id(udev, &hori->input->id);

	hori->input->open = hori_open;
	hori->input->close = hori_close;

	input_set_abs_params(hori->input, ABS_X, 0, 255, 0, 0);
	input_set_abs_params(hori->input, ABS_Y, 0, 255, 0, 0);
	input_set_abs_params(hori->input, ABS_RX, 0, 255, 0, 0);
	input_set_abs_params(hori->input, ABS_RY, 0, 255, 0, 0);
	input_set_abs_params(hori->input, ABS_THROTTLE, 0, 255, 0, 0);
	input_set_abs_params(hori->input, ABS_RUDDER, 0, 255, 0, 0);
	//input_set_abs_params(hori->input, ABS_TILT_X, 0, 3, 0, 0);
	//input_set_abs_params(hori->input, ABS_TILT_Y, 0, 3, 0, 0);
	input_set_abs_params(hori->input, ABS_Z, 0, 3, 0, 0);
	input_set_abs_params(hori->input, ABS_RZ, 0, 3, 0, 0);

	input_set_capability(hori->input, EV_KEY, BTN_TRIGGER_HAPPY1);
	input_set_capability(hori->input, EV_KEY, BTN_TRIGGER_HAPPY2);
	input_set_capability(hori->input, EV_KEY, BTN_TRIGGER_HAPPY3);
	input_set_capability(hori->input, EV_KEY, BTN_TRIGGER_HAPPY4);

	input_set_capability(hori->input, EV_KEY, BTN_TRIGGER_HAPPY5);
	input_set_capability(hori->input, EV_KEY, BTN_TRIGGER_HAPPY6);
	input_set_capability(hori->input, EV_KEY, BTN_TRIGGER_HAPPY7);
	input_set_capability(hori->input, EV_KEY, BTN_TRIGGER_HAPPY8);

	input_set_capability(hori->input, EV_KEY, BTN_TRIGGER);
	input_set_capability(hori->input, EV_KEY, BTN_THUMB);
	input_set_capability(hori->input, EV_KEY, BTN_THUMB2);
	input_set_capability(hori->input, EV_KEY, BTN_A);
	input_set_capability(hori->input, EV_KEY, BTN_B);
	input_set_capability(hori->input, EV_KEY, BTN_C);
	input_set_capability(hori->input, EV_KEY, BTN_X);
	input_set_capability(hori->input, EV_KEY, BTN_Y);
	
	/* MODE switch, not currently used. Gotta make it Press/Unpress or something?
	 *
	input_set_capability(hori->input, EV_KEY, BTN_BASE);
	input_set_capability(hori->input, EV_KEY, BTN_GEAR_UP);
	input_set_capability(hori->input, EV_KEY, BTN_GEAR_DOWN);
	*/
/*
	input_set_capability(hori->input, EV_KEY, BTN_Z);
	input_set_capability(hori->input, EV_KEY, BTN_TL);
	input_set_capability(hori->input, EV_KEY, BTN_TR);
	input_set_capability(hori->input, EV_KEY, BTN_MODE);
*/
	//input_set_abs_params(hori->input, ABS_MISC, 0, 255, 0, 0);

	input_set_drvdata(hori->input, hori);

	error = input_register_device(hori->input);
	if (error)
		return error;

	return 0;
}

static void hori_disconnect(struct usb_interface *intf)
{
	/* All driver resources are devm-managed. */
}

static int hori_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct hori *hori = usb_get_intfdata(intf);

	dev_warn(&hori->intf->dev,
		"%s - usb_kill_urb\n",
		__func__);
	guard(mutex)(&hori->pm_mutex);
	if (hori->is_open) {
		usb_kill_urb(hori->urb);
		usb_kill_urb(hori->urb_ctl);
	}

	return 0;
}

static int hori_resume(struct usb_interface *intf)
{
	struct hori *hori = usb_get_intfdata(intf);

	guard(mutex)(&hori->pm_mutex);
	if (hori->is_open && usb_submit_urb(hori->urb, GFP_KERNEL) < 0)
		return -EIO;

	if (hori->is_open) {
		hori_poll_vr0(hori);
		//hori_poll_vr1(hori->input);
	}

	return 0;
}

static int hori_pre_reset(struct usb_interface *intf)
{
	struct hori *hori = usb_get_intfdata(intf);

	dev_warn(&hori->intf->dev,
		"%s - usb_kill_urb\n",
		__func__);
	mutex_lock(&hori->pm_mutex);
	usb_kill_urb(hori->urb);
	usb_kill_urb(hori->urb_ctl);
	return 0;
}

static int hori_post_reset(struct usb_interface *intf)
{
	struct hori *hori = usb_get_intfdata(intf);
	int retval = 0;

	if (hori->is_open && usb_submit_urb(hori->urb, GFP_KERNEL) < 0)
		retval = -EIO;

	if (hori->is_open) {
		hori_poll_vr0(hori);
		//hori_poll_vr1(hori->input);
	}

	mutex_unlock(&hori->pm_mutex);

	return retval;
}

static int hori_reset_resume(struct usb_interface *intf)
{
	return hori_resume(intf);
}

static const struct usb_device_id hori_table[] = {
	{ USB_DEVICE(HORI_VENDOR_ID, HORI_PRODUCT_ID) },
	{ }
};
MODULE_DEVICE_TABLE(usb, hori_table);

static struct usb_driver hori_driver = {
	.name =		"hori",
	.probe =	hori_probe,
	.disconnect =	hori_disconnect,
	.id_table =	hori_table,
	.suspend	= hori_suspend,
	.resume		= hori_resume,
	.pre_reset	= hori_pre_reset,
	.post_reset	= hori_post_reset,
	.reset_resume	= hori_reset_resume,
};

module_usb_driver(hori_driver);

MODULE_AUTHOR("Daniel O'Neill <daniel@oneill.app>");
MODULE_DESCRIPTION("Mitsubishi Hori/Namco Flightstick");
MODULE_LICENSE("GPL v2");
