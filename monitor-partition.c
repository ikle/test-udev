#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <err.h>
#include <poll.h>

#include <libudev.h>

static int scan_partitions (struct udev *u,
			    void (*process) (struct udev_device *dev))
{
	struct udev_enumerate *e;
	int status;
	struct udev_list_entry *list, *i;

	if ((e = udev_enumerate_new (u)) == NULL)
		return -ENODEV;

	if ((status = udev_enumerate_add_match_subsystem (e, "block")) != 0 ||
	    (status = udev_enumerate_add_match_property (e, "DEVTYPE", "partition")) != 0 ||
	    (status = udev_enumerate_scan_devices (e)) != 0)
		goto no_enumerate;

	list = udev_enumerate_get_list_entry (e);

	udev_list_entry_foreach (i, list) {
		const char *path = udev_list_entry_get_name (i);
		struct udev_device *dev;

		if ((dev = udev_device_new_from_syspath (u, path)) == NULL)
			continue;

		process (dev);

		udev_device_unref (dev);
	}

	udev_enumerate_unref (e);
	return 0;
no_enumerate:
	udev_enumerate_unref (e);
	return status;
}

static int monitor_partitions (struct udev *u,
			       void (*process) (struct udev_device *dev))
{
	struct udev_monitor *m;
	int status;

	if ((m = udev_monitor_new_from_netlink (u, "udev")) == NULL)
		return -ENODEV;

	if ((status = udev_monitor_filter_add_match_subsystem_devtype (m, "block", "partition")) != 0 ||
	    (status = udev_monitor_enable_receiving (m)) != 0)
		goto no_start;

	for (;;) {
		struct pollfd fds = {udev_monitor_get_fd (m), POLLIN, };
		struct udev_device *dev;

		if (poll (&fds, 1, -1) <= 0)
			break;

		if ((dev = udev_monitor_receive_device (m)) == NULL)
			continue;

		process (dev);

		udev_device_unref (dev);
	}

	udev_monitor_unref (m);
	return 0;
no_start:
	udev_monitor_unref (m);
	return status;
}

static void print_partition_number (struct udev_device *dev,
				    const char *prefix)
{
	const char *n;

	if ((n = udev_device_get_sysattr_value (dev, "partition")) != NULL)
		printf ("%s%s", prefix, n);
}

static size_t str_len (const char *s)
{
	const char *tail, *p;

	for (p = tail = s; *p != '\0'; ++p)
		if (!isspace (*p))
			tail = p + 1;

	return tail - s;
}

static void print_scsi_device_info (struct udev_device *dev,
				    const char *prefix)
{
	struct udev_device *parent;
	const char *vendor, *model;

	parent = udev_device_get_parent_with_subsystem_devtype (dev, "scsi", "scsi_device");
	if (parent == NULL)
		return;

	vendor = udev_device_get_sysattr_value (parent, "vendor");
	model  = udev_device_get_sysattr_value (parent, "model");

	printf ("%s%.*s %.*s", prefix, str_len (vendor), vendor,
		str_len (model),  model);
}

static void print_usb_device_info (struct udev_device *dev,
				   const char *prefix)
{
	struct udev_device *parent;
	const char *busnum, *devpath;

	parent = udev_device_get_parent_with_subsystem_devtype (dev, "usb", "usb_device");
	if (parent == NULL)
		return;

	busnum  = udev_device_get_sysattr_value (parent, "busnum");
	devpath = udev_device_get_sysattr_value (parent, "devpath");

	printf ("%s%s-%s", prefix, busnum, devpath);
}

static void process (struct udev_device *dev)
{
	const char *action;

	if ((action = udev_device_get_action (dev)) == NULL)
		action = "add";

	printf ("%s: %s", action, udev_device_get_devnode (dev));

	if (strcmp ("add", action) == 0) {
		print_partition_number (dev, ", partition ");
		print_scsi_device_info (dev, ", device ");
		print_usb_device_info  (dev, " on USB ");
	}

	printf ("\n");
}

int main (int argc, char *argv[])
{
	struct udev *u;

	if ((u = udev_new ()) == NULL)
		errx (1, "cannot create udev context");

	if (scan_partitions (u, process) != 0)
		errx (1, "cannot scan partitions");

	if (monitor_partitions (u, process) != 0)
		errx (1, "cannot monitor partitions");

	udev_unref (u);
	return 0;
}
