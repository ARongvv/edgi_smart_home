/* MiLoCo spec/status/control protocol checks; no network or OS required. */
#include <stdio.h>
#include <string.h>
#include <agent/error.h>
#include "port/sca_os.h"
#include "smart_home/miloco/smart_home_miloco_protocol.h"
#include "smart_home/miloco/smart_home_miloco_specscan.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

double trunc(double value)
{
    return (double)(long long)value;
}

void sca_log(int level, const char *tag, const char *fmt, ...)
{
    (void)level;
    (void)tag;
    (void)fmt;
}

int main(void)
{
    static const char spec[] =
        "{\"code\":0,\"data\":{\"did\":\"mock-light-1\",\"category\":\"light\","
        "\"spec\":{\"prop.2.1\":{\"type_name\":\"on\",\"description\":\"Power\","
        "\"format\":\"bool\",\"readable\":true,\"writeable\":true},"
        "\"prop.2.2\":{\"type_name\":\"brightness\",\"format\":\"uint8\","
        "\"readable\":true,\"writeable\":true,\"value_range\":[1,100,1]}}}}";
    static const char status[] =
        "{\"code\":0,\"data\":{\"properties\":["
        "{\"iid\":\"prop.2.1\",\"value\":true,\"code\":0},"
        "{\"iid\":\"prop.2.2\",\"value\":72,\"code\":0}]}}";
    static const char control[] =
        "{\"code\":0,\"data\":{\"results\":[{\"did\":\"mock-light-1\","
        "\"iid\":\"prop.2.1\",\"code\":0}]}}";
    smart_home_miloco_device_t device;
    char did[24];
    smart_home_miloco_category_t category;
    bool power_control;
    bool changed;

    memset(&device, 0, sizeof(device));
    CHECK(smart_home_miloco_specscan(spec, did, sizeof(did), &category,
        device.controls, SMART_HOME_MILOCO_MAX_CONTROLS,
        &device.control_count, &power_control) == 0);
    CHECK(strcmp(did, "mock-light-1") == 0);
    CHECK(category == SMART_HOME_MILOCO_CATEGORY_LIGHT);
    CHECK(device.control_count == 2 && power_control);
    CHECK(smart_home_miloco_power_index(&device) == 0);
    CHECK(smart_home_miloco_status_apply(&device, status, &changed) == AGENT_OK);
    CHECK(changed && device.power_valid && device.power_on);
    CHECK(device.controls[1].value_valid && device.controls[1].value == 72);
    CHECK(smart_home_miloco_value_allowed(&device.controls[1], 100));
    CHECK(!smart_home_miloco_value_allowed(&device.controls[1], 0));
    CHECK(smart_home_miloco_control_reply(control, "mock-light-1",
                                           "prop.2.1", false) == AGENT_OK);
    CHECK(smart_home_miloco_control_reply(control, "other-device",
                                           "prop.2.1", false) == AGENT_ERROR_PARSE);
    printf("test_miloco_protocol: spec, readback and control validation passed\n");
    return 0;
}
