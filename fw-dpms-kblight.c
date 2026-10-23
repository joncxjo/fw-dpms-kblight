#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define DPMS_STATE_FILE "/sys/class/drm/card0-eDP-1/dpms"
#define CROS_EC_DEVICE "/dev/cros_ec"

// The code below (between the "BEGIN COPIED CODE" and "END COPIED CODE" comments) was copied from
// the "ChromiumOS Embedded Controller software" repo at commit d67134434d72. URLs to the original
// files in that repo are above each snippet below. The original code is "Copyright (c) 2013-2014
// The Chromium OS Authors" and is licensed under a "BSD-style license" that can be found at the
// following URL: https://chromium.googlesource.com/chromiumos/platform/ec/+/d67134434d72/LICENSE

// BEGIN COPIED CODE FROM https://chromium.googlesource.com/chromiumos/platform/ec

// https://chromium.googlesource.com/chromiumos/platform/ec/+/d67134434d72/util/cros_ec_dev.h
struct cros_ec_command_v2 {
    uint32_t version;
    uint32_t command;
    uint32_t outsize;
    uint32_t insize;
    uint32_t result;
    // Replace zero-length array with flexible-array member
    // https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/commit/?id=120088832042
    uint8_t data[];
};
#define CROS_EC_DEV_IOC_V2 0xEC
#define CROS_EC_DEV_IOCXCMD_V2 _IOWR(CROS_EC_DEV_IOC_V2, 0, struct cros_ec_command_v2)

// https://chromium.googlesource.com/chromiumos/platform/ec/+/d67134434d72/include/ec_commands.h
#define EC_RES_SUCCESS 0 // from enum ec_status
#define EC_CMD_PWM_SET_DUTY 0x0025
#define EC_PWM_MAX_DUTY 0xffff
#define EC_PWM_TYPE_KB_LIGHT 1 // from enum ec_pwm_type
struct ec_params_pwm_set_duty {
    uint16_t duty;
    uint8_t pwm_type;
    uint8_t index;
} __attribute__((packed)) __attribute__((aligned(4)));

// END COPIED CODE FROM https://chromium.googlesource.com/chromiumos/platform/ec

// max length of dpms state, i.e. { 'O', 'f', 'f', '\n', '\0' }
#define DPMS_STATE_MAXLEN 5

typedef struct iso_timestamp_t {
    char data[26];
} iso_timestamp_t;

// generate an ISO 8601 timestamp with local timezone offset from a time_t
static int
iso_timestamp(time_t t, iso_timestamp_t *out)
{
    struct tm *lt = localtime(&t);
    strftime(out->data, sizeof(iso_timestamp_t), "%Y-%m-%dT%H:%M:%S%z", lt);
    // shift and insert colon in timezone offset, like "-0500" -> "-05:00"
    out->data[24] = out->data[23];
    out->data[23] = out->data[22];
    out->data[22] = ':';
    out->data[25] = '\0';
    return 0;
}

// log to stdout when something happens
static void
log_state(char *dpms_state, uint16_t kblight_duty)
{
    char dpms_state_json[6] = { 0 };
    if (dpms_state == NULL) {
        strcpy(dpms_state_json, "null");
    } else if (strcmp(dpms_state, "On\n") == 0) {
        strcpy(dpms_state_json, "\"on\"");
    } else if (strcmp(dpms_state, "Off\n") == 0) {
        strcpy(dpms_state_json, "\"off\"");
    } else {
        strcpy(dpms_state_json, "\"???\"");
    }

    // convert duty to percent
    uint8_t kblight_pct = (uint8_t) (((double) kblight_duty / (double) EC_PWM_MAX_DUTY) * 100.0);

    iso_timestamp_t timestamp;
    iso_timestamp(time(NULL), &timestamp);

    printf(
        "{\"timestamp\": \"%s\", \"dpms_state\": %s, \"kblight\": %u}\n",
        (char *) &timestamp, dpms_state_json, kblight_pct);
}

// set the keyboard backlight brightness
static int
set_kblight(
    int ec_fd, struct cros_ec_command_v2 *ec_cmd, struct ec_params_pwm_set_duty *ec_params,
    uint16_t duty)
{
    ec_params->duty = duty;
    if (ioctl(ec_fd, CROS_EC_DEV_IOCXCMD_V2, ec_cmd) < 0) {
        perror("Error sending EC command via ioctl");
        return errno;
    }
    if (ec_cmd->result != EC_RES_SUCCESS) {
        fprintf(stderr, "Error response code from EC command: %u\n", ec_cmd->result);
        return -1;
    }
    return 0;
}

int
main(void)
{
    // make stdout and stderr line buffered
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    int dpms_fd = -1, ec_fd = -1;
    struct cros_ec_command_v2 *ec_cmd = NULL;
    struct ec_params_pwm_set_duty *ec_params = NULL;

    // open the needed files
    dpms_fd = open(DPMS_STATE_FILE, O_RDONLY);
    if (dpms_fd < 0) { perror("Error opening file " DPMS_STATE_FILE); goto fail; }
    ec_fd = open(CROS_EC_DEVICE, O_RDWR);
    if (ec_fd < 0) { perror("Error opening file " CROS_EC_DEVICE); goto fail; }

    // initialize the EC command and params
    ec_cmd = calloc(1, sizeof(*ec_cmd) + sizeof(*ec_params));
    if (ec_cmd == NULL) { perror("Error allocating memory for EC command"); goto fail; }
    ec_cmd->command = EC_CMD_PWM_SET_DUTY;
    ec_cmd->outsize = sizeof(*ec_params);
    ec_params = (struct ec_params_pwm_set_duty *) ec_cmd->data;
    ec_params->pwm_type = EC_PWM_TYPE_KB_LIGHT;

    // NOTE: the open files and allocated memory above lives for the entire
    // life of the process, so there's no cleanup needed below

    // set the initial brightness to 100%
    log_state(NULL, EC_PWM_MAX_DUTY);
    if (set_kblight(ec_fd, ec_cmd, ec_params, EC_PWM_MAX_DUTY) != 0) { goto fail; }

    // wait a little bit before starting to watch for anything
    sleep(10);

    ssize_t num_read;
    char dpms_state[DPMS_STATE_MAXLEN] = { 0 };
    uint16_t kblight_duty = EC_PWM_MAX_DUTY, prev_kblight_duty = 0;

    // main loop
    for (;;) {
        // read the dpms state from sysfs
        num_read = read(dpms_fd, dpms_state, DPMS_STATE_MAXLEN);
        if (num_read == 0) {
            fprintf(stderr, "Error reading file " DPMS_STATE_FILE ": Zero bytes read\n");
            goto fail;
        }
        if (num_read < 0) { perror("Error reading file " DPMS_STATE_FILE); goto fail; }

        // what should the current duty be based on the dpms state?
        kblight_duty = (strcmp(dpms_state, "On\n") == 0) ? EC_PWM_MAX_DUTY : 0;

        if (kblight_duty != prev_kblight_duty) {
            // set the keyboard backlight brightness to the new value
            log_state(dpms_state, kblight_duty);
            if (set_kblight(ec_fd, ec_cmd, ec_params, kblight_duty) != 0) { goto fail; }
        }

        // get ready for the next iteration
        prev_kblight_duty = kblight_duty;
        memset(dpms_state, 0, DPMS_STATE_MAXLEN);
        lseek(dpms_fd, 0, SEEK_SET);

        // this seems to be more than enough to avoid any significant resource
        // usage by this loop, and the delay isn't really noticeable
        sleep(1);
    }

    return 0;

fail:
    fprintf(stderr, "ABORTING!\n");
    return 1;
}
