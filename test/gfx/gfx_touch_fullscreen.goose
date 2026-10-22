// A touch against a mouse (mouse_touch), the touch screen query, and the
// fullscreen state, headless.
import gfx from "../..";
import std;
import os;

fn main() {
    guard gfx::open_headless(64, 64) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    gfx::frame();
    print("touch screen: ", gfx::touch_screen());
    gfx::inject_mouse(10.0, 12.0, 0, false);
    gfx::frame();
    print("mouse move: touch ", gfx::mouse_touch(), " at ", gfx::mouse_pos().x, ",", gfx::mouse_pos().y);
    gfx::inject_touch(30.0, 40.0, true);
    gfx::frame();
    print("touch down: touch ", gfx::mouse_touch(), " pressed ", gfx::mouse_pressed(gfx::MOUSE_LEFT),
          " at ", gfx::mouse_pos().x, ",", gfx::mouse_pos().y);
    gfx::inject_touch(30.0, 40.0, false);
    gfx::frame();
    print("touch up: touch ", gfx::mouse_touch(), " released ", gfx::mouse_released(gfx::MOUSE_LEFT));
    gfx::inject_mouse(20.0, 20.0, gfx::MOUSE_LEFT, true);
    gfx::frame();
    print("click: touch ", gfx::mouse_touch(), " pressed ", gfx::mouse_pressed(gfx::MOUSE_LEFT));
    print("fullscreen: ", gfx::fullscreen());
    print("set: ", gfx::set_fullscreen(true), " now ", gfx::fullscreen());
    print("set: ", gfx::set_fullscreen(false), " now ", gfx::fullscreen());
    gfx::close();
}
