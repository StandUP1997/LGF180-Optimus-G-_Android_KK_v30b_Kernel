/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/diagchar.h>
#include <linux/kmemleak.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/ratelimit.h>
#include <linux/platform_device.h>
#include <linux/smux.h>
#ifdef CONFIG_DIAG_OVER_USB
#include <mach/usbdiag.h>
#endif
#include "diagchar.h"
#include "diagmem.h"
#include "diagfwd_cntl.h"
#include "diagfwd_smux.h"
#include "diagfwd_hsic.h"
#include "diag_masks.h"
#include "diagfwd_bridge.h"

#include "mtsk_tty.h"
struct diag_bridge_dev *diag_bridge;

/* diagfwd_connect_bridge is called when the USB mdm channel is connected */
int diagfwd_connect_bridge(int process_cable)
{
	int i;

	pr_debug("diag: in %s\n", __func__);

	for (i = 0; i < MAX_BRIDGES; i++)
		if (diag_bridge[i].enabled)
			connect_bridge(process_cable, i);
	return 0;
}

void connect_bridge(int process_cable, int index)
{
	int err;

//2012-03-06 seongmook.yim(seongmook.yim@lge.com) [P6/MDMBSP] ADD LGODL [START]
#ifdef CONFIG_LGE_DM_DEV
			if ((process_cable) && (driver->logging_mode == DM_DEV_MODE)) {
				printk(KERN_DEBUG "diag: USB connected in DM_DEV_MODE\n");
				diag_bridge[index].usb_connected = 1;
			}
#endif /*CONFIG_LGE_DM_DEV*/
//2012-03-06 seongmook.yim(seongmook.yim@lge.com) [P6/MDMBSP] ADD LGODL [END]


#ifdef CONFIG_LGE_DM_APP
	if ((process_cable) && (driver->logging_mode == DM_APP_MODE)) {
		printk(KERN_DEBUG "diag: USB connected in DM_APP_MODE\n");
		diag_bridge[index].usb_connected = 1;
	}
#endif /*CONFIG_LGE_DM_APP*/


	mutex_lock(&diag_bridge[index].bridge_mutex);
	/* If the usb cable is being connected */
	if (process_cable) {
		err = usb_diag_alloc_req(diag_bridge[index].ch, N_MDM_WRITE,
			       N_MDM_READ);
		if (err)
			pr_err("diag: unable to alloc USB req for ch %d err:%d\n",
							 index, err);

		diag_bridge[index].usb_connected = 1;
	}

	if (index == SMUX && driver->diag_smux_enabled) {
		driver->in_busy_smux = 0;
		diagfwd_connect_smux();
	} else {
		if (diag_hsic[index].hsic_device_enabled &&
			(driver->logging_mode != MEMORY_DEVICE_MODE ||
			diag_hsic[index].hsic_data_requested)) {
			diag_hsic[index].in_busy_hsic_read_on_device = 0;
			diag_hsic[index].in_busy_hsic_write = 0;
			/* If the HSIC (diag_bridge) platform
			 * device is not open */
			if (!diag_hsic[index].hsic_device_opened) {
				hsic_diag_bridge_ops[index].ctxt =
							(void *)(index);
				err = diag_bridge_open(index,
						 &hsic_diag_bridge_ops[index]);
				if (err) {
					pr_err("diag: HSIC channel open error: %d\n",
						   err);
				} else {
					pr_debug("diag: opened HSIC channel\n");
					diag_hsic[index].hsic_device_opened =
									1;
				}
			} else {
				pr_debug("diag: HSIC channel already open\n");
			}
			/*
			 * Turn on communication over usb mdm and HSIC,
			 * if the HSIC device driver is enabled
			 * and opened
			 */
			if (diag_hsic[index].hsic_device_opened) {
				diag_hsic[index].hsic_ch = 1;
				/* Poll USB mdm channel to check for data */
				if (driver->logging_mode == USB_MODE)
					queue_work(diag_bridge[index].wq,
						   &diag_bridge[index].
							diag_read_work);
				/* Poll HSIC channel to check for data */
				queue_work(diag_bridge[index].wq,
					   &diag_hsic[index].
					   diag_read_hsic_work);
			}
		}
	}
	mutex_unlock(&diag_bridge[index].bridge_mutex);
}

/*
 * diagfwd_disconnect_bridge is called when the USB mdm channel
 * is disconnected. So disconnect should happen for all bridges
 */
int diagfwd_disconnect_bridge(int process_cable)
{
	int i;
	pr_debug("diag: In %s, process_cable: %d\n", __func__, process_cable);
//2012-03-06 seongmook.yim(seongmook.yim@lge.com) [P6/MDMBSP] ADD LGODL [START]
#ifdef CONFIG_LGE_DM_DEV
		if (driver->logging_mode == DM_DEV_MODE) {
			for (i = 0; i < MAX_BRIDGES; i++) {
				mutex_lock(&diag_bridge[i].bridge_mutex);
				if (diag_bridge[i].enabled) {
					diag_bridge[i].usb_connected = 0;
					}
				mutex_unlock(&diag_bridge[i].bridge_mutex);
				}
			return 0;
		}
#endif /*CONFIG_LGE_DM_DEV*/
//2012-03-06 seongmook.yim(seongmook.yim@lge.com) [P6/MDMBSP] ADD LGODL [END]


#ifdef CONFIG_LGE_DM_APP
			if (driver->logging_mode == DM_APP_MODE) {
				for (i = 0; i < MAX_BRIDGES; i++) {
					mutex_lock(&diag_bridge[i].bridge_mutex);
					if (diag_bridge[i].enabled) {
						diag_bridge[i].usb_connected = 0;
						}
					mutex_unlock(&diag_bridge[i].bridge_mutex);
					}
				return 0;
			}
#endif /*CONFIG_LGE_DM_APP*/


	for (i = 0; i < MAX_BRIDGES; i++) {
		if (diag_bridge[i].enabled) {
			mutex_lock(&diag_bridge[i].bridge_mutex);
			/* If the usb cable is being disconnected */
			if (process_cable) {
				diag_bridge[i].usb_connected = 0;
				usb_diag_free_req(diag_bridge[i].ch);
			}

			if (i == SMUX) {
				if (driver->diag_smux_enabled &&
					driver->logging_mode == USB_MODE) {
					driver->in_busy_smux = 1;
					driver->lcid = LCID_INVALID;
					driver->smux_connected = 0;
					/*
					 * Turn off communication over usb
					 * and smux
					 */
					msm_smux_close(LCID_VALID);
				}
			}  else {
				if (diag_hsic[i].hsic_device_enabled &&
				     (driver->logging_mode != MEMORY_DEVICE_MODE
				     || !diag_hsic[i].hsic_data_requested)) {
					diag_hsic[i].
						in_busy_hsic_read_on_device = 1;
					diag_hsic[i].in_busy_hsic_write = 1;
					/* Turn off communication over usb
					 * and HSIC */
					diag_hsic_close(i);
				}
			}
			mutex_unlock(&diag_bridge[i].bridge_mutex);
		}
	}
	return 0;
}

/* Called after the asychronous usb_diag_read() on mdm channel is complete */
int diagfwd_read_complete_bridge(struct diag_request *diag_read_ptr)
{
	 int index = (int)(diag_read_ptr->context);

	/* The read of the usb on the mdm (not HSIC/SMUX) has completed */
	diag_bridge[index].read_len = diag_read_ptr->actual;

	if (index == SMUX) {
		if (driver->diag_smux_enabled) {
			diagfwd_read_complete_smux();
			return 0;
		} else {
			pr_warning("diag: incorrect callback for smux\n");
		}
	}

	/* If SMUX not enabled, check for HSIC */
	diag_hsic[index].in_busy_hsic_read_on_device = 0;
	if (!diag_hsic[index].hsic_ch) {
		pr_err("DIAG in %s: hsic_ch == 0, ch %d\n", __func__, index);
		return 0;
	}

	/*
	 * The read of the usb driver on the mdm channel has completed.
	 * If there is no write on the HSIC in progress, check if the
	 * read has data to pass on to the HSIC. If so, pass the usb
	 * mdm data on to the HSIC.
	 */
	if (!diag_hsic[index].in_busy_hsic_write &&
		diag_bridge[index].usb_buf_out &&
		(diag_bridge[index].read_len > 0)) {

		/*
		 * Initiate the HSIC write. The HSIC write is
		 * asynchronous. When complete the write
		 * complete callback function will be called
		 */
		int err;
		diag_hsic[index].in_busy_hsic_write = 1;
		err = diag_bridge_write(index, diag_bridge[index].usb_buf_out,
					diag_bridge[index].read_len);
		if (err) {
			pr_err_ratelimited("diag: mdm data on HSIC write err: %d\n",
					err);
			/*
			 * If the error is recoverable, then clear
			 * the write flag, so we will resubmit a
			 * write on the next frame.  Otherwise, don't
			 * resubmit a write on the next frame.
			 */
			if ((-ENODEV) != err)
				diag_hsic[index].in_busy_hsic_write = 0;
		}
	}

	/*
	 * If there is no write of the usb mdm data on the
	 * HSIC channel
	 */
	if (!diag_hsic[index].in_busy_hsic_write)
		queue_work(diag_bridge[index].wq,
			 &diag_bridge[index].diag_read_work);

	return 0;
}

static void diagfwd_bridge_notifier(void *priv, unsigned event,
					struct diag_request *d_req)
{
	int index;

	switch (event) {
	case USB_DIAG_CONNECT:
		queue_work(driver->diag_wq,
			 &driver->diag_connect_work);
		break;
	case USB_DIAG_DISCONNECT:
		queue_work(driver->diag_wq,
			 &driver->diag_disconnect_work);
		break;
	case USB_DIAG_READ_DONE:
		index = (int)(d_req->context);
		queue_work(diag_bridge[index].wq,
		&diag_bridge[index].usb_read_complete_work);
		break;
	case USB_DIAG_WRITE_DONE:
		index = (int)(d_req->context);
		if (index == SMUX && driver->diag_smux_enabled)
			diagfwd_write_complete_smux();
		else if (diag_hsic[index].hsic_device_enabled)
			diagfwd_write_complete_hsic(d_req, index);
		break;
	default:
		pr_err("diag: in %s: Unknown event from USB diag:%u\n",
			__func__, event);
		break;
	}
}

void diagfwd_bridge_init(int index)
{
	int ret;
	unsigned char name[20];

	if (index == HSIC) {
		strlcpy(name, "hsic", sizeof(name));
	} else if (index == HSIC_2) {
		strlcpy(name, "hsic_2", sizeof(name));
	} else if (index == SMUX) {
		strlcpy(name, "smux", sizeof(name));
//2012-03-06 seongmook.yim(seongmook.yim@lge.com) [P6/MDMBSP] ADD LGODL [START]
#if defined(CONFIG_LGE_DM_DEV) || defined(CONFIG_LGE_DM_APP)

	} else if (index == HSIC_3) {
		strlcpy(name, "hsic_3", sizeof(name));
	} else if (index == HSIC_4) {
		strlcpy(name, "hsic_4", sizeof(name));
#endif /* CONFIG_LGE_DM_DEV */
//2012-03-06 seongmook.yim(seongmook.yim@lge.com) [P6/MDMBSP] ADD LGODL [END]
	} else {
		pr_err("diag: incorrect bridge init, instance: %d\n", index);
		return;
	}

	strlcpy(diag_bridge[index].name, name,
				sizeof(diag_bridge[index].name));
	strlcat(name, "_diag_wq", sizeof(diag_bridge[index].name));
	diag_bridge[index].id = index;
	diag_bridge[index].wq = create_singlethread_workqueue(name);
	diag_bridge[index].read_len = 0;
	diag_bridge[index].write_len = 0;
	if (diag_bridge[index].usb_buf_out == NULL)
		diag_bridge[index].usb_buf_out =
				 kzalloc(USB_MAX_OUT_BUF, GFP_KERNEL);
	if (diag_bridge[index].usb_buf_out == NULL)
		goto err;
	if (diag_bridge[index].usb_read_ptr == NULL)
		diag_bridge[index].usb_read_ptr =
			 kzalloc(sizeof(struct diag_request), GFP_KERNEL);
	if (diag_bridge[index].usb_read_ptr == NULL)
		goto err;
	if (diag_bridge[index].usb_read_ptr->context == NULL)
		diag_bridge[index].usb_read_ptr->context =
					 kzalloc(sizeof(int), GFP_KERNEL);
	if (diag_bridge[index].usb_read_ptr->context == NULL)
		goto err;
	mutex_init(&diag_bridge[index].bridge_mutex);

	if (index == HSIC || index == HSIC_2) {
		INIT_WORK(&(diag_bridge[index].usb_read_complete_work),
				 diag_usb_read_complete_hsic_fn);
#ifdef CONFIG_DIAG_OVER_USB
		INIT_WORK(&(diag_bridge[index].diag_read_work),
		      diag_read_usb_hsic_work_fn);
		if (index == HSIC)
			diag_bridge[index].ch = usb_diag_open(DIAG_MDM,
				 (void *)index, diagfwd_bridge_notifier);
		else if (index == HSIC_2)
			diag_bridge[index].ch = usb_diag_open(DIAG_MDM2,
				 (void *)index, diagfwd_bridge_notifier);
		if (IS_ERR(diag_bridge[index].ch)) {
			pr_err("diag: Unable to open USB MDM ch = %d\n", index);
			goto err;
		} else
			diag_bridge[index].enabled = 1;
#endif
	} else if (index == SMUX) {
		INIT_WORK(&(diag_bridge[index].usb_read_complete_work),
					 diag_usb_read_complete_smux_fn);
#ifdef CONFIG_DIAG_OVER_USB
		INIT_WORK(&(diag_bridge[index].diag_read_work),
					 diag_read_usb_smux_work_fn);
		diag_bridge[index].ch = usb_diag_open(DIAG_QSC, (void *)index,
					     diagfwd_bridge_notifier);
		if (IS_ERR(diag_bridge[index].ch)) {
			pr_err("diag: Unable to open USB diag QSC channel\n");
			goto err;
		} else
			diag_bridge[index].enabled = 1;
#endif
		ret = platform_driver_register(&msm_diagfwd_smux_driver);
		if (ret)
			pr_err("diag: could not register SMUX device, ret: %d\n",
									 ret);
	}
	 return;
err:
	pr_err("diag: Could not initialize for bridge forwarding\n");
	kfree(diag_bridge[index].usb_buf_out);
	kfree(diag_hsic[index].hsic_buf_tbl);
	kfree(driver->write_ptr_mdm);
	kfree(diag_bridge[index].usb_read_ptr);
	if (diag_bridge[index].wq)
		destroy_workqueue(diag_bridge[index].wq);
	return;
}

void diagfwd_bridge_exit(void)
{
	int i;
	pr_debug("diag: in %s\n", __func__);

	for (i = 0; i < MAX_HSIC_CH; i++) {
		if (diag_hsic[i].hsic_device_enabled) {
			diag_hsic_close(i);
			diag_hsic[i].hsic_device_enabled = 0;
			diag_bridge[i].enabled = 0;
		}
		diag_hsic[i].hsic_inited = 0;
		kfree(diag_hsic[i].hsic_buf_tbl);
	}
	diagmem_exit(driver, POOL_TYPE_ALL);
	if (driver->diag_smux_enabled) {
		driver->lcid = LCID_INVALID;
		kfree(driver->buf_in_smux);
		driver->diag_smux_enabled = 0;
		diag_bridge[SMUX].enabled = 0;
	}
	platform_driver_unregister(&msm_hsic_ch_driver);
	platform_driver_unregister(&msm_diagfwd_smux_driver);
	/* destroy USB MDM specific variables */
	for (i = 0; i < MAX_BRIDGES; i++) {
		if (diag_bridge[i].enabled) {
#ifdef CONFIG_DIAG_OVER_USB
			if (diag_bridge[i].usb_connected)
				usb_diag_free_req(diag_bridge[i].ch);
			usb_diag_close(diag_bridge[i].ch);
#endif
			kfree(diag_bridge[i].usb_buf_out);
			kfree(diag_bridge[i].usb_read_ptr);
			destroy_workqueue(diag_bridge[i].wq);
			diag_bridge[i].enabled = 0;
		}
	}
	kfree(driver->write_ptr_mdm);
}
int mtsk_tty_send_mask(struct diag_request *diag_read_ptr) {


	return diagfwd_read_complete_bridge(diag_read_ptr);
}

