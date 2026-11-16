#pragma once
#include <stdint.h>

enum class ControlOp : uint8_t {
    Files, Start, Pause, Resume, Cancel, Home, Cool, Mesh, Shaper, Offsets,
    Bed55, Bed75, Nozzle200, Nozzle235, XMinus, XPlus, YMinus, YPlus, ZMinus, ZPlus,
    FanOff, FanHalf, FanFull, MotorsOff, SelectTool,
    FanSet /* value = ventilador * 1000 + porcentagem */
};

struct ControlFiles {
    static constexpr int limit = 24;
    uint32_t version;
    int count;
    bool more;
    char paths[limit][192];
};

bool control_submit(ControlOp op, int value = 0, const char *path = nullptr);
void control_copy_files(ControlFiles &out);
bool control_estop();
bool control_estop_result(int &code);
void control_ui_build();
void control_ui_tick();
struct _lv_event_t;
void ui_go_control(_lv_event_t *);
