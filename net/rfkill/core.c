/*
 * Copyright (C) 2006 - 2007 Ivo van Doorn
 * Copyright (C) 2007 Dmitry Torokhov
 * Copyright 2009 Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/capability.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rfkill.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/miscdevice.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/slab.h>

#include "rfkill.h"

#define POLL_INTERVAL		(5 * HZ)

#define RFKILL_BLOCK_HW		BIT(0)
#define RFKILL_BLOCK_SW		BIT(1)
#define RFKILL_BLOCK_SW_PREV	BIT(2)
#define RFKILL_BLOCK_ANY	(RFKILL_BLOCK_HW |\
				 RFKILL_BLOCK_SW |\
				 RFKILL_BLOCK_SW_PREV)
#define RFKILL_BLOCK_SW_SETCALL	BIT(31)

struct rfkill {
	spinlock_t		lock;

	const char		*name;
	enum rfkill_type	type;

	unsigned long		state;

	u32			idx;

	bool			registered;
	bool			persistent;

	const struct rfkill_ops	*ops;
	void			*data;

#ifdef CONFIG_RFKILL_LEDS
	struct led_trigger	led_trigger;
	const char		*ledtrigname;
#endif

	struct device		dev;
	struct list_head	node;

	struct delayed_work	poll_work;
	struct work_struct	uevent_work;
	struct work_struct	sync_work;
};
#define to_rfkill(d)	container_of(d, struct rfkill, dev)

struct rfkill_int_event {
	struct list_head	list;
	struct rfkill_event	ev;
};

struct rfkill_data {
	struct list_head	list;
	struct list_head	events;
	struct mutex		mtx;
	wait_queue_head_t	read_wait;
	bool			input_handler;
};


MODULE_AUTHOR("Ivo van Doorn <IvDoorn@gmail.com>");
MODULE_AUTHOR("Johannes Berg <johannes@sipsolutions.net>");
MODULE_DESCRIPTION("RF switch support");
MODULE_LICENSE("GPL");


/*
 * The locking here should be made much smarter, we currently have
 * a bit of a stupid situation because drivers might want to register
 * the rfkill struct under their own lock, and take this lock during
 * rfkill method calls -- which will cause an AB-BA deadlock situation.
 *
 * To fix that, we need to rework this code here to be mostly lock-free
 * and only use the mutex for list manipulations, not to protect the
 * various other global variables. Then we can avoid holding the mutex
 * around driver operations, and all is happy.
 */
static LIST_HEAD(rfkill_list);	/* list of registered rf switches */
static DEFINE_MUTEX(rfkill_global_mutex);
static LIST_HEAD(rfkill_fds);	/* list of open fds of /dev/rfkill */

static unsigned int rfkill_default_state = 1;
module_param_named(default_state, rfkill_default_state, uint, 0444);
MODULE_PARM_DESC(default_state,
		 "Default initial state for all radio types, 0 = radio off");

static struct {
	bool cur, sav;
} rfkill_global_states[NUM_RFKILL_TYPES];

static bool rfkill_epo_lock_active;


#ifdef CONFIG_RFKILL_LEDS
static void rfkill_led_trigger_event(struct rfkill *rfkill)
{
	struct led_trigger *trigger;

	if (!rfkill->registered)
		return;

	trigger = &rfkill->led_trigger;

	if (rfkill->state & RFKILL_BLOCK_ANY)
		led_trigger_event(trigger, LED_OFF);
	else
		led_trigger_event(trigger, LED_FULL);
}

static void rfkill_led_trigger_activate(struct led_classdev *led)
{
	struct rfkill *rfkill;

	rfkill = container_of(led->trigger, struct rfkill, led_trigger);

	rfkill_led_trigger_event(rfkill);
}

static int rfkill_led_trigger_register(struct rfkill *rfkill)
{
	rfkill->led_trigger.name = rfkill->ledtrigname
					? : dev_name(&rfkill->dev);
	rfkill->led_trigger.activate = rfkill_led_trigger_activate;
	return led_trigger_register(&rfkill->led_trigger);
}

static void rfkill_led_trigger_unregister(struct rfkill *rfkill)
{
	led_trigger_unregister(&rfkill->led_trigger);
}
#else
static void rfkill_led_trigger_event(struct rfkill *rfkill)
{
}

static inline int rfkill_led_trigger_register(struct rfkill *rfkill)
{
	return 0;
}

static inline void rfkill_led_trigger_unregister(struct rfkill *rfkill)
{
}
#endif /* CONFIG_RFKILL_LEDS */

static void rfkill_fill_event(struct rfkill_event *ev, struct rfkill *rfkill,
			      enum rfkill_operation op)
{
	unsigned long flags;

	ev->idx = rfkill->idx;
	ev->type = rfkill->type;
	ev->op = op;

	spin_lock_irqsave(&rfkill->lock, flags);
	ev->hard = !!(rfkill->state & RFKILL_BLOCK_HW);
	ev->soft = !!(rfkill->state & (RFKILL_BLOCK_SW |
					RFKILL_BLOCK_SW_PREV));
	spin_unlock_irqrestore(&rfkill->lock, flags);
}

static void rfkill_send_events(struct rfkill *rfkill, enum rfkill_operation op)
{
	struct rfkill_data *data;
	struct rfkill_int_event *ev;

	list_for_each_entry(data, &rfkill_fds, list) {
		ev = kzalloc(sizeof(*ev), GFP_KERNEL);
		if (!ev)
			continue;
		rfkill_fill_event(&ev->ev, rfkill, op);
		mutex_lock(&data->mtx);
		list_add_tail(&ev->list, &data->events);
		mutex_unlock(&data->mtx);
		wake_up_interruptible(&data->read_wait);
	}
}

static void rfkill_event(struct rfkill *rfkill)
{
	if (!rfkill->registered)
		return;

	kobject_uevent(&rfkill->dev.kobj, KOBJ_CHANGE);

	/* also send event to /dev/rfkill */
	rfkill_send_events(rfkill, RFKILL_OP_CHANGE);
}

static bool __rfkill_set_hw_state(struct rfkill *rfkill,
				  bool blocked, bool *change)
{
	unsigned long flags;
	bool prev, any;

	BUG_ON(!rfkill);

	spin_lock_irqsave(&rfkill->lock, flags);
	prev = !!(rfkill->state & RFKILL_BLOCK_HW);
	if (blocked)
		rfkill->state |= RFKILL_BLOCK_HW;
	else
		rfkill->state &= ~RFKILL_BLOCK_HW;
	*change = prev != blocked;
	any = rfkill->state & RFKILL_BLOCK_ANY;
	spin_unlock_irqrestore(&rfkill->lock, flags);

	rfkill_led_trigger_event(rfkill);

	return any;
}

/**
 * rfkill_set_block - wrapper for set_block method
 *
 * @rfkill: the rfkill struct to use
 * @blocked: the new software state
 *
 * Calls the set_block method (when applicable) and handles notifications
 * etc. as well.
 */
static void rfkill_set_block(struct rfkill *rfkill, bool blocked)
{
	unsigned long flags;
	int err;

	if (unlikely(rfkill->dev.power.power_state.event & PM_EVENT_SLEEP))
		return;

	/*
	 * Some platforms (...!) generate input events which affect the
	 * _hard_ kill state -- whenever something tries to change the
	 * current software state query the hardware state too.
	 */
	if (rfkill->ops->query)
		rfkill->ops->query(rfkill, rfkill->data);

	spin_lock_irqsave(&rfkill->lock, flags);
	if (rfkill->state & RFKILL_BLOCK_SW)
		rfkill->state |= RFKILL_BLOCK_SW_PREV;
	else
		rfkill->state &= ~RFKILL_BLOCK_SW_PREV;

	if (blocked)
		rfkill->state |= RFKILL_BLOCK_SW;
	else
		rfkill->state &= ~RFKILL_BLOCK_SW;

	rfkill->state |= RFKILL_BLOCK_SW_SETCALL;
	spin_unlock_irqrestore(&rfkill->lock, flags);

	err = rfkill->ops->set_block(rfkill->data, blocked);

	spin_lock_irqsave(&rfkill->lock, flags);
	if (err) {
		/*
		 * Failed -- reset status to _prev, this may be different
		 * from what set set _PREV to earlier in this function
		 * if rfkill_set_sw_state was invoked.
		 */
		if (rfkill->state & RFKILL_BLOCK_SW_PREV)
			rfkill->state |= RFKILL_BLOCK_SW;
		else
			rfkill->state &= ~RFKILL_BLOCK_SW;
	}
	rfkill->state &= ~RFKILL_BLOCK_SW_SETCALL;
	rfkill->state &= ~RFKILL_BLOCK_SW_PREV;

	spin_unlock_irqrestore(&rfkill->lock, flags);

	rfkill_led_trigger_event(rfkill);

	rfkill_event(rfkill);
}

#ifdef CONFIG_RFKILL_INPUT
static atomic_t rfkill_input_disabled = ATOMIC_INIT(0);

/**
 * __rfkill_switch_all - Toggle state of all switches of given type
 * @type: type of interfaces to be affected
 * @state: the new state
 *
 * This function sets the state of all switches of given type,
 * unless a specific switch is claimed by userspace (in which case,
 * that switch is left alone) or suspended.
 *
 * Caller must have acquired rfkill_global_mutex.
 */
static void __rfkill_switch_all(const enum rfkill_type type, bool blocked)
{
	struct rfkill *rfkill;

	rfkill_global_states[type].cur = blocked;
	list_for_each_entry(rfkill, &rfkill_list, node) {
		if (rfkill->type != type)
			continue;

		rfkill_set_block(rfkill, blocked);
	}
}

/**
 * rfkill_switch_all - Toggle state of all switches of given type
 * @type: type of interfaces to be affected
 * @state: the new state
 *
 * Acquires rfkill_global_mutex and calls __rfkill_switch_all(@type, @state).
 * Please refer to __rfkill_switch_all() for details.
 *
 * Does nothing if the EPO lock is active.
 */
void rfkill_switch_all(enum rfkill_type type, bool blocked)
{
	if (atomic_read(&rfkill_input_disabled))
		return;

	mutex_lock(&rfkill_global_mutex);

	if (!rfkill_epo_lock_active)
		__rfkill_switch_all(type, blocked);

	mutex_unlock(&rfkill_global_mutex);
}

/**
 * rfkill_epo - emergency power off all transmitters
 *
 * This kicks all non-suspended rfkill devices to RFKILL_STATE_SOFT_BLOCKED,
 * ignoring everything in its path but rfkill_global_mutex and rfkill->mutex.
 *
 * The global state before the EPO is saved and can be restored later
 * using rfkill_restore_states().
 */
void rfkill_epo(void)
{
	struct rfkill *rfkill;
	int i;

	if (atomic_read(&rfkill_input_disabled))
		return;

	mutex_lock(&rfkill_global_mutex);

	rfkill_epo_lock_active = true;
	list_for_each_entry(rfkill, &rfkill_list, node)
		rfkill_set_block(rfkill, true);

	for (i = 0; i < NUM_RFKILL_TYPES; i++) {
		rfkill_global_states[i].sav = rfkill_global_states[i].cur;
		rfkill_global_states[i].cur = true;
	}

	mutex_unlock(&rfkill_global_mutex);
}

/**
 * rfkill_restore_states - restore global states
 *
 * Restore (and sync switches to) the global state from the
 * states in rfkill_default_states.  This can undo the effects of
 * a call to rfkill_epo().
 */
void rfkill_restore_states(void)
{
	int i;

	if (atomic_read(&rfkill_input_disabled))
		return;

	mutex_lock(&rfkill_global_mutex);

	rfkill_epo_lock_active = false;
	for (i = 0; i < NUM_RFKILL_TYPES; i++)
		__rfkill_switch_all(i, rfkill_global_states[i].sav);
	mutex_unlock(&rfkill_global_mutex);
}

/**
 * rfkill_remove_epo_lock - unlock state changes
 *
 * Used by rfkill-input manually unlock state changes, when
 * the EPO switch is deactivated.
 */
void rfkill_remove_epo_lock(void)
{
	if (atomic_read(&rfkill_input_disabled))
		return;

	mutex_lock(&rfkill_global_mutex);
	rfkill_epo_lock_active = false;
	mutex_unlock(&rfkill_global_mutex);
}

/**
 * rfkill_is_epo_lock_active - returns true EPO is active
 *
 * Returns 0 (false) if there is NOT an active EPO contidion,
 * and 1 (true) if there is an active EPO contition, which
 * locks all radios in one of the BLOCKED states.
 *
 * Can be called in atomic context.
 */
bool rfkill_is_epo_lock_active(void)
{
	return rfkill_epo_lock_active;
}

/**
 * rfkill_get_global_sw_state - returns global state for a type
 * @type: the type to get the global state of
 *
 * Returns the current global state for a given wireless
 * device type.
 */
bool rfkill_get_global_sw_state(const enum rfkill_type type)
{
	return rfkill_global_states[type].cur;
}
#endif


bool rfkill_set_hw_state(struct rfkill *rfkill, bool blocked)
{
	bool ret, change;

	ret = __rfkill_set_hw_state(rfkill, blocked, &change);

	if (!rfkill->registered)
		return ret;

	if (change)
		schedule_work(&rfkill->uevent_work);

	return ret;
}
EXPORT_SYMBOL(rfkill_set_hw_state);

static void __rfkill_set_sw_state(struct rfkill *rfkill, bool blocked)
{
	u32 bit = RFKILL_BLOCK_SW;

	/* if in a ops->set_block right now, use other bit */
	if (rfkill->state & RFKILL_BLOCK_SW_SETCALL)
		bit = RFKILL_BLOCK_SW_PREV;

	if (blocked)
		rfkill->state |= bit;
	else
		rfkill->state &= ~bit;
}

bool rfkill_set_sw_state(struct rfkill *rfkill, bool blocked)
{
	unsigned long flags;
	bool prev, hwblock;

	BUG_ON(!rfkill);

	spin_lock_irqsave(&rfkill->lock, flags);
	prev = !!(rfkill->state & RFKILL_BLOCK_SW);
	__rfkill_set_sw_state(rfkill, blocked);
	hwblock = !!(rfkill->state & RFKILL_BLOCK_HW);
	blocked = blocked || hwblock;
	spin_unlock_irqrestore(&rfkill->lock, flags);

	if (!rfkill->registered)
		return blocked;

	if (prev != blocked && !hwblock)
		schedule_work(&rfkill->uevent_work);

	rfkill_led_trigger_event(rfkill);

	return blocked;
}
EXPORT_SYMBOL(rfkill_set_sw_state);

void rfkill_init_sw_state(struct rfkill *rfkill, bool blocked)
{
	unsigned long flags;

	BUG_ON(!rfkill);
	BUG_ON(rfkill->registered);

	spin_lock_irqsave(&rfkill->lock, flags);
	__rfkill_set_sw_state(rfkill, blocked);
	rfkill->persistent = true;
	spin_unlock_irqrestore(&rfkill->lock, flags);
}
EXPORT_SYMBOL(rfkill_init_sw_state);

void rfkill_set_states(struct rfkill *rfkill, bool sw, bool hw)
{
	unsigned long flags;
	bool swprev, hwprev;

	BUG_ON(!rfkill);

	spin_lock_irqsave(&rfkill->lock, flags);

	/*
	 * No need to care about prev/setblock ... this is for uevent only
	 * and that will get triggered by rfkill_set_block anyway.
	 */
	swprev = !!(rfkill->state & RFKILL_BLOCK_SW);
	hwprev = !!(rfkill->state & RFKILL_BLOCK_HW);
	__rfkill_set_sw_state(rfkill, sw);
	if (hw)
		rfkill->state |= RFKILL_BLOCK_HW;
	else
		rfkill->state &= ~RFKILL_BLOCK_HW;

	spin_unlock_irqrestore(&rfkill->lock, flags);

	if (!rfkill->registered) {
		rfkill->persistent = true;
	} else {
		if (swprev != sw || hwprev != hw)
			schedule_work(&rfkill->uevent_work);

		rfkill_led_trigger_event(rfkill);
	}
}
EXPORT_SYMBOL(rfkill_set_states);

static ssize_t rfkill_name_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rfkill *rfkill = to_rfkill(dev);

	return sprintf(buf, "%s\n", rfkill->name);
}

static const char *rfkill_get_type_str(enum rfkill_type type)
{
	BUILD_BUG_ON(NUM_RFKILL_TYPES != RFKILL_TYPE_FM + 1);

	switch (type) {
	case RFKILL_TYPE_WLAN:
		return "wlan";
	case RFKILL_TYPE_BLUETOOTH:
		return "bluetooth";
	case RFKILL_TYPE_UWB:
		return "ultrawideband";
	case RFKILL_TYPE_WIMAX:
		return "wimax";
	case RFKILL_TYPE_WWAN:
		return "wwan";
	case RFKILL_TYPE_GPS:
		return "gps";
	case RFKILL_TYPE_FM:
		return "fm";
	default:
		BUG();
	}
}

static ssize_t rfkill_type_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rfkill *rfkill = to_rfkill(dev);

	return sprintf(buf, "%s\n", rfkill_get_type_str(rfkill->type));
}

static ssize_t rfkill_idx_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct rfkill *rfkill = to_rfkill(dev);

	return sprintf(buf, "%d\n", rfkill->idx);
}

static ssize_t rfkill_persistent_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct rfkill *rfkill = to_rfkill(dev);

	return sprintf(buf, "%d\n", rfkill->persistent);
}

static ssize_t rfkill_hard_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct rfkill *rfkill = to_rfkill(dev);

	return sprintf(buf, "%d\n", (rfkill->state & RFKILL_BLOCK_HW) ? 1 : 0 );
}

static ssize_t rfkill_soft_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct rfkill *rfkill = to_rfkill(dev);

	return sprintf(buf, "%d\n", (rfkill->state & RFKILL_BLOCK_SW) ? 1 : 0 );
}

static ssize_t rfkill_soft_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct rfkill *rfkill = to_rfkill(dev);
	unsigned long state;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	err = strict_strtoul(buf, 0, &state);
	if (err)
		return err;

	if (state > 1 )
		return -EINVAL;

	mutex_lock(&rfkill_global_mutex);
	rfkill_set_block(rfkill, state);
	mutex_unlock(&rfkill_global_mutex);

	return err ?: count;
}

static u8 user_state_from_blocked(unsigned long state)
{
	if (state & RFKILL_BLOCK_HW)
		return RFKILL_USER_STATE_HARD_BLOCKED;
	if (state & RFKILL_BLOCK_SW)
		return RFKILL_USER_STATE_SOFT_BLOCKED;

	return RFKILL_USER_STATE_UNBLOCKED;
}

static ssize_t rfkill_state_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct rfkill *rfkill = to_rfkill(dev);

	return sprintf(buf, "%d\n", user_state_from_blocked(rfkill->state));
}

static ssize_t rfkill_state_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct rfkill *rfkill = to_rfkill(dev);
	unsigned long state;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	err = strict_strtoul(buf, 0, &state);
	if (err)
		return err;

	if (state != RFKILL_USER_STATE_SOFT_BLOCKED &&
	    state != RFKILL_USER_STATE_UNBLOCKED)
		return -EINVAL;

	mutex_lock(&rfkill_global_mutex);
	rfkill_set_block(rfkill, state == RFKILL_USER_STATE_SOFT_BLOCKED);
	mutex_unlock(&rfkill_global_mutex);

	return err ?: count;
}

static ssize_t rfkill_claim_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	return sprintf(buf, "%d\n", 0);
}

static ssize_t rfkill_claim_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	return -EOPNOTSUPP;
}

static struct device_attribute rfkill_dev_attrs[] = {
	__ATTR(name, S_IRUGO, rfkill_name_show, NULL),
	__ATTR(type, S_IRUGO, rfkill_type_show, NULL),
	__ATTR(index, S_IRUGO, rfkill_idx_show, NULL),
	__ATTR(persistent, S_IRUGO, rfkill_persistent_show, NULL),
	__ATTR(state, S_IRUGO|S_IWUSR, rfkill_state_show, rfkill_state_store),
	__ATTR(claim, S_IRUGO|S_IWUSR, rfkill_claim_show, rfkill_claim_store),
	__ATTR(soft, S_IRUGO|S_IWUSR, rfkill_soft_show, rfkill_soft_store),
	__ATTR(hard, S_IRUGO, rfkill_hard_show, NULL),
	__ATTR_NULL
};

static void rfkill_release(struct device *dev)
{
	struct rfkill *rfkill = to_rfkill(dev);

	kfree(rfkill);
}

static int rfkill_dev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct rfkill *rfkill = to_rfkill(dev);
	unsigned long flags;
	u32 state;
	int error;

	error = add_uevent_var(env, "RFKILL_NAME=%s", rfkill->name);
	if (error)
		return error;
	error = add_uevent_var(env, "RFKILL_TYPE=%s",
			       rfkill_get_type_str(rfkill->type));
	if (error)
		return error;
	spin_lock_irqsave(&rfkill->lock, flags);
	state = rfkill->state;
	spin_unlock_irqrestore(&rfkill->lock, flags);
	error = add_uevent_var(env, "RFKILL_STATE=%d",
			       user_state_from_blocked(state));
	return error;
}

void rfkill_pause_polling(struct rfkill *rfkill)
{
	BUG_ON(!rfkill);

	if (!rfkill->ops->poll)
		return;

	cancel_delayed_work_sync(&rfkill->poll_work);
}
EXPORT_SYMBOL(rfkill_pause_polling);

#ifdef CONFIG_RFKILL_PM
void rfkill_resume_polling(struct rfkill *rfkill)
{
	BUG_ON(!rfkill);

	if (!rfkill->ops->poll)
		return;

	schedule_work(&rfkill->poll_work.work);
}
EXPORT_SYMBOL(rfkill_resume_polling);

static int rfkill_suspend(struct device *dev, pm_message_t state)
{
	struct rfkill *rfkill = to_rfkill(dev);

	rfkill_pause_polling(rfkill);

	return 0;
}

static int rfkill_resume(struct device *dev)
{
	struct rfkill *rfkill = to_rfkill(dev);
	bool cur;

	if (!rfkill->persistent) {
		cur = !!(rfkill->state & RFKILL_BLOCK_SW);
		rfkill_set_block(rfkill, cur);
	}

	rfkill_resume_polling(rfkill);

	return 0;
}
#endif

static struct class rfkill_class = {
	.name		= "rfkill",
	.dev_release	= rfkill_release,
	.dev_attrs	= rfkill_dev_attrs,
	.dev_uevent	= rfkill_dev_uevent,
#ifdef CONFIG_RFKILL_PM
	.suspend	= rfkill_suspend,
	.resume		= rfkill_resume,
#endif
};

bool rfkill_blocked(struct rfkill *rfkill)
{
	unsigned long flags;
	u32 state;

	spin_lock_irqsave(&rfkill->lock, flags);
	state = rfkill->state;
	spin_unlock_irqrestore(&rfkill->lock, flags);

	return !!(state & RFKILL_BLOCK_ANY);
}
EXPORT_SYMBOL(rfkill_blocked);


struct rfkill * __must_check rfkill_alloc(const char *name,
					  struct device *parent,
					  const enum rfkill_type type,
					  const struct rfkill_ops *ops,
					  void *ops_data)
{
	struct rfkill *rfkill;
	struct device *dev;

	if (WARN_ON(!ops))
		return NULL;

	if (WARN_ON(!ops->set_block))
		return NULL;

	if (WARN_ON(!name))
		return NULL;

	if (WARN_ON(type == RFKILL_TYPE_ALL || type >= NUM_RFKILL_TYPES))
		return NULL;

	rfkill = kzalloc(sizeof(*rfkill), GFP_KERNEL);
	if (!rfkill)
		return NULL;

	spin_lock_init(&rfkill->lock);
	INIT_LIST_HEAD(&rfkill->node);
	rfkill->type = type;
	rfkill->name = name;
	rfkill->ops = ops;
	rfkill->data = ops_data;

	dev = &rfkill->dev;
	dev->class = &rfkill_class;
	dev->parent = parent;
	device_initialize(dev);

	return rfkill;
}
EXPORT_SYMBOL(rfkill_alloc);

static void rfkill_poll(struct work_struct *work)
{
	struct rfkill *rfkill;

	rfkill = container_of(work, struct rfkill, poll_work.work);

	/*
	 * Poll hardware state -- driver will use one of the
	 * rfkill_set{,_hw,_sw}_state functions and use its
	 * return value to update the current status.
	 */
	rfkill->ops->poll(rfkill, rfkill->data);

	schedule_delayed_work(&rfkill->poll_work,
		round_jiffies_relative(POLL_INTERVAL));
}

static void rfkill_uevent_work(struct work_struct *work)
{
	struct rfkill *rfkill;

	rfkill = container_of(work, struct rfkill, uevent_work);

	mutex_lock(&rfkill_global_mutex);
	rfkill_event(rfkill);
	mutex_unlock(&rfkill_global_mutex);
}

static void rfkill_sync_work(struct work_struct *work)
{
	struct rfkill *rfkill;
	bool cur;

	rfkill = container_of(work, struct rfkill, sync_work);

	mutex_lock(&rfkill_global_mutex);
	cur = rfkill_global_states[rfkill->type].cur;
	rfkill_set_block(rfkill, cur);
	mutex_unlock(&rfkill_global_mutex);
}

int __must_check rfkill_register(struct rfkill *rfkill)
{
	static unsigned long rfkill_no;
	struct device *dev = &rfkill->dev;
	int error;

	BUG_ON(!rfkill);

	mutex_lock(&rfkill_global_mutex);

	if (rfkill->registered) {
		error = -EALREADY;
		goto unlock;
	}

	rfkill->idx = rfkill_no;
	dev_set_name(dev, "rfkill%lu", rfkill_no);
	rfkill_no++;

	list_add_tail(&rfkill->node, &rfkill_list);

	error = device_add(dev);
	if (error)
		goto remove;

	error = rfkill_led_trigger_register(rfkill);
	if (error)
		goto devdel;

	rfkill->registered = true;

	INIT_DELAYED_WORK(&rfkill->poll_work, rfkill_poll);
	INIT_WORK(&rfkill->uevent_work, rfkill_uevent_work);
	INIT_WORK(&rfkill->sync_work, rfkill_sync_work);

	if (rfkill->ops->poll)
		schedule_delayed_work(&rfkill->poll_work,
			round_jiffies_relative(POLL_INTERVAL));

	if (!rfkill->persistent || rfkill_epo_lock_active) {
		schedule_work(&rfkill->sync_work);
	} else {
#ifdef CONFIG_RFKILL_INPUT
		bool soft_blocked = !!(rfkill->state & RFKILL_BLOCK_SW);

		if (!atomic_read(&rfkill_input_disabled))
			__rfkill_switch_all(rfkill->type, soft_blocked);
#endif
	}

	rfkill_send_events(rfkill, RFKILL_OP_ADD);

	mutex_unlock(&rfkill_global_mutex);
	return 0;

 devdel:
	device_del(&rfkill->dev);
 remove:
	list_del_init(&rfkill->node);
 unlock:
	mutex_unlock(&rfkill_global_mutex);
	return error;
}
EXPORT_SYMBOL(rfkill_register);

void rfkill_unregister(struct rfkill *rfkill)
{
	BUG_ON(!rfkill);

	if (rfkill->ops->poll)
		cancel_delayed_work_sync(&rfkill->poll_work);

	cancel_work_sync(&rfkill->uevent_work);
	cancel_work_sync(&rfkill->sync_work);

	rfkill->registered = false;

	device_del(&rfkill->dev);

	mutex_lock(&rfkill_global_mutex);
	rfkill_send_events(rfkill, RFKILL_OP_DEL);
	list_del_init(&rfkill->node);
	mutex_unlock(&rfkill_global_mutex);

	rfkill_led_trigger_unregister(rfkill);
}
EXPORT_SYMBOL(rfkill_unregister);

void rfkill_destroy(struct rfkill *rfkill)
{
	if (rfkill)
		put_device(&rfkill->dev);
}
EXPORT_SYMBOL(rfkill_destroy);

static int rfkill_fop_open(struct inode *inode, struct file *file)
{
	struct rfkill_data *data;
	struct rfkill *rfkill;
	struct rfkill_int_event *ev, *tmp;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	INIT_LIST_HEAD(&data->events);
	mutex_init(&data->mtx);
	init_waitqueue_head(&data->read_wait);

	mutex_lock(&rfkill_global_mutex);
	mutex_lock(&data->mtx);
	/*
	 * start getting events from elsewhere but hold mtx to get
	 * startup events added first
	 */

	list_for_each_entry(rfkill, &rfkill_list, node) {
		ev = kzalloc(sizeof(*ev), GFP_KERNEL);
		if (!ev)
			goto free;
		rfkill_fill_event(&ev->ev, rfkill, RFKILL_OP_ADD);
		list_add_tail(&ev->list, &data->events);
	}
	list_add(&data->list, &rfkill_fds);
	mutex_unlock(&data->mtx);
	mutex_unlock(&rfkill_global_mutex);

	file->private_data = data;

	return nonseekable_open(inode, file);

 free:
	mutex_unlock(&data->mtx);
	mutex_unlock(&rfkill_global_mutex);
	mutex_destroy(&data->mtx);
	list_for_each_entry_safe(ev, tmp, &data->events, list)
		kfree(ev);
	kfree(data);
	return -ENOMEM;
}

static unsigned int rfkill_fop_poll(struct file *file, poll_table *wait)
{
	struct rfkill_data *data = file->private_data;
	unsigned int res = POLLOUT | POLLWRNORM;

	poll_wait(file, &data->read_wait, wait);

	mutex_lock(&data->mtx);
	if (!list_empty(&data->events))
		res = POLLIN | POLLRDNORM;
	mutex_unlock(&data->mtx);

	return res;
}

static bool rfkill_readable(struct rfkill_data *data)
{
	bool r;

	mutex_lock(&data->mtx);
	r = !list_empty(&data->events);
	mutex_unlock(&data->mtx);

	return r;
}

static ssize_t rfkill_fop_read(struct file *file, char __user *buf,
			       size_t count, loff_t *pos)
{
	struct rfkill_data *data = file->private_data;
	struct rfkill_int_event *ev;
	unsigned long sz;
	int ret;

	mutex_lock(&data->mtx);

	while (list_empty(&data->events)) {
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto out;
		}
		mutex_unlock(&data->mtx);
		ret = wait_event_interruptible(data->read_wait,
					       rfkill_readable(data));
		mutex_lock(&data->mtx);

		if (ret)
			goto out;
	}

	ev = list_first_entry(&data->events, struct rfkill_int_event,
				list);

	sz = min_t(unsigned long, sizeof(ev->ev), count);
	ret = sz;
	if (copy_to_user(buf, &ev->ev, sz))
		ret = -EFAULT;

	list_del(&ev->list);
	kfree(ev);
 out:
	mutex_unlock(&data->mtx);
	return ret;
}

static ssize_t rfkill_fop_write(struct file *file, const char __user *buf,
				size_t count, loff_t *pos)
{
	struct rfkill *rfkill;
	struct rfkill_event ev;

	/* we don't need the 'hard' variable but accept it */
	if (count < RFKILL_EVENT_SIZE_V1 - 1)
		return -EINVAL;

	/*
	 * Copy as much data as we can accept into our 'ev' buffer,
	 * but tell userspace how much we've copied so it can determine
	 * our API version even in a write() call, if it cares.
	 */
	count = min(count, sizeof(ev));
	if (copy_from_user(&ev, buf, count))
		return -EFAULT;

	if (ev.op != RFKILL_OP_CHANGE && ev.op != RFKILL_OP_CHANGE_ALL)
		return -EINVAL;

	if (ev.type >= NUM_RFKILL_TYPES)
		return -EINVAL;

	mutex_lock(&rfkill_global_mutex);

	if (ev.op == RFKILL_OP_CHANGE_ALL) {
		if (ev.type == RFKILL_TYPE_ALL) {
			enum rfkill_type i;
			for (i = 0; i < NUM_RFKILL_TYPES; i++)
				rfkill_global_states[i].cur = ev.soft;
		} else {
			rfkill_global_states[ev.type].cur = ev.soft;
		}
	}

	list_for_each_entry(rfkill, &rfkill_list, node) {
		if (rfkill->idx != ev.idx && ev.op != RFKILL_OP_CHANGE_ALL)
			continue;

		if (rfkill->type != ev.type && ev.type != RFKILL_TYPE_ALL)
			continue;

		rfkill_set_block(rfkill, ev.soft);
	}
	mutex_unlock(&rfkill_global_mutex);

	return count;
}

static int rfkill_fop_release(struct inode *inode, struct file *file)
{
	struct rfkill_data *data = file->private_data;
	struct rfkill_int_event *ev, *tmp;

	mutex_lock(&rfkill_global_mutex);
	list_del(&data->list);
	mutex_unlock(&rfkill_global_mutex);

	mutex_destroy(&data->mtx);
	list_for_each_entry_safe(ev, tmp, &data->events, list)
		kfree(ev);

#ifdef CONFIG_RFKILL_INPUT
	if (data->input_handler)
		if (atomic_dec_return(&rfkill_input_disabled) == 0)
			printk(KERN_DEBUG "rfkill: input handler enabled\n");
#endif

	kfree(data);

	return 0;
}

#ifdef CONFIG_RFKILL_INPUT
static long rfkill_fop_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	struct rfkill_data *data = file->private_data;

	if (_IOC_TYPE(cmd) != RFKILL_IOC_MAGIC)
		return -ENOSYS;

	if (_IOC_NR(cmd) != RFKILL_IOC_NOINPUT)
		return -ENOSYS;

	mutex_lock(&data->mtx);

	if (!data->input_handler) {
		if (atomic_inc_return(&rfkill_input_disabled) == 1)
			printk(KERN_DEBUG "rfkill: input handler disabled\n");
		data->input_handler = true;
	}

	mutex_unlock(&data->mtx);

	return 0;
}
#endif

static const struct file_operations rfkill_fops = {
	.owner		= THIS_MODULE,
	.open		= rfkill_fop_open,
	.read		= rfkill_fop_read,
	.write		= rfkill_fop_write,
	.poll		= rfkill_fop_poll,
	.release	= rfkill_fop_release,
#ifdef CONFIG_RFKILL_INPUT
	.unlocked_ioctl	= rfkill_fop_ioctl,
	.compat_ioctl	= rfkill_fop_ioctl,
#endif
	.llseek		= no_llseek,
};

static struct miscdevice rfkill_miscdev = {
	.name	= "rfkill",
	.fops	= &rfkill_fops,
	.minor	= MISC_DYNAMIC_MINOR,
};

static int __init rfkill_init(void)
{
	int error;
	int i;

	for (i = 0; i < NUM_RFKILL_TYPES; i++)
		rfkill_global_states[i].cur = !rfkill_default_state;

	error = class_register(&rfkill_class);
	if (error)
		goto out;

	error = misc_register(&rfkill_miscdev);
	if (error) {
		class_unregister(&rfkill_class);
		goto out;
	}

#ifdef CONFIG_RFKILL_INPUT
	error = rfkill_handler_init();
	if (error) {
		misc_deregister(&rfkill_miscdev);
		class_unregister(&rfkill_class);
		goto out;
	}
#endif

 out:
	return error;
}
subsys_initcall(rfkill_init);

static void __exit rfkill_exit(void)
{
#ifdef CONFIG_RFKILL_INPUT
	rfkill_handler_exit();
#endif
	misc_deregister(&rfkill_miscdev);
	class_unregister(&rfkill_class);
}
module_exit(rfkill_exit);
