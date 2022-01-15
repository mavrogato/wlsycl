
#include <iostream>
#include <string_view>

#include <CL/sycl.hpp>

//!!!
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
//!!!

#include "wayland-client-helper.hpp"

int main() {
    static auto xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    auto display = attach_unique(wl_display_connect(nullptr));
    auto globals = register_global<wl_compositor, wl_shell, wl_shm, wl_seat>(display.get());
    auto& [compositor, shell, shm, seat] = globals;
    {
        static constexpr wl_shm_listener listener {
            .format = [](auto, auto, uint32_t format) noexcept {
                std::cout << "format supported: " << format << std::endl;
            },
        };
        std::cout << "retrieving memory formats..." << std::endl;
        wl_shm_add_listener(shm.get(), &listener, nullptr);
        wl_display_roundtrip(display.get());
        wl_display_roundtrip(display.get());
        wl_display_roundtrip(display.get());
        wl_display_roundtrip(display.get());
    }
    auto surface = attach_unique(wl_compositor_create_surface(compositor.get()));
    auto shell_surface = attach_unique(wl_shell_get_shell_surface(shell.get(), surface.get()));
    {
        static constexpr wl_shell_surface_listener listener {
            .ping = [](auto, auto shell_surface, auto serial) noexcept {
                wl_shell_surface_pong(shell_surface, serial);
                std::cerr << "pinged and ponged." << std::endl;
            },
            .configure  = [](auto...) noexcept { },
            .popup_done = [](auto...) noexcept { },
        };
        wl_shell_surface_add_listener(shell_surface.get(), &listener, nullptr);
    }
    wl_shell_surface_set_toplevel(shell_surface.get());
    {
        static constexpr wl_seat_listener listener {
            .capabilities = [](void*, wl_seat* seat_raw, uint32_t caps) noexcept {
                if (caps & WL_SEAT_CAPABILITY_POINTER) {
                    std::cout << "*** pointer device found." << std::endl;
                }
                if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
                    std::cout << "*** keyboard device found." << std::endl;
                }
                if (caps & WL_SEAT_CAPABILITY_TOUCH) {
                    std::cout << "*** touch device found." << std::endl;
                }
                std::cout << "seat capability: " << caps << std::endl;
            },
            .name = [](void*, wl_seat* seat_raw, char const* name) noexcept {
                std::cout << name << std::endl;
            },
        };
        if (0 != wl_seat_add_listener(seat.get(), &listener, nullptr)) {
            std::cerr << "wl_seat_add_listener failed..." << std::endl;
            return -1;
        }
    }
    static int key_input = 0;
    auto keyboard = attach_unique(wl_seat_get_keyboard(seat.get()));
    {
        static constexpr wl_keyboard_listener listener {
            .keymap = [](auto...) noexcept { },
            .enter = [](auto...) noexcept { },
            .leave = [](auto...) noexcept { },
            .key = [](void *data,
                      wl_keyboard* keyboard_raw,
                      uint32_t serial,
                      uint32_t time,
                      uint32_t key,
                      uint32_t state) noexcept
            {
                key_input = key;
            },
            .modifiers = [](auto...) noexcept { },
            .repeat_info = [](auto...) noexcept { },
        };
        if (0 != wl_keyboard_add_listener(keyboard.get(), &listener, nullptr)) {
            std::cerr << "wl_keyboard_add_listener failed..." << std::endl;
            return -1;
        }
    }
    auto pointer = attach_unique(wl_seat_get_pointer(seat.get()));
    {
        static constexpr wl_pointer_listener listener {
            .enter = [](auto...) noexcept { },
            .leave = [](auto...) noexcept { },
            .motion = [](auto...) noexcept { },
            .button = [](auto...) noexcept { std::cerr << "button" << std::endl; },
            .axis = [](auto...) noexcept { },
            .frame = [](auto...) noexcept { },
            .axis_source = [](auto...) noexcept { },
            .axis_stop = [](auto...) noexcept { },
            .axis_discrete = [](auto...) noexcept { },
        };
        wl_pointer_add_listener(pointer.get(), &listener, nullptr);
    }
    auto touch = attach_unique(wl_seat_get_touch(seat.get()));
    {
        static constexpr wl_touch_listener listener {
            .down = [](auto...) noexcept { },
            .up = [](auto...) noexcept { },
            .motion = [](auto...) noexcept { },
            .frame = [](auto...) noexcept { },
            .cancel = [](auto...) noexcept { },
            .shape = [](auto...) noexcept { },
            .orientation = [](auto...) noexcept { },
        };
        wl_touch_add_listener(touch.get(), &listener, nullptr);
    }
    constexpr static auto create_buffer = [](wl_shm* shm, int cx, int cy) noexcept
        -> std::pair<decltype (attach_unique(std::declval<wl_buffer*>())), uint32_t*>
    {
        static constexpr std::string_view tmp_name = "/weston-sahred-XXXXXX";
        std::string tmp_path(xdg_runtime_dir);
        tmp_path += tmp_name;
        int fd = mkostemp(tmp_path.data(), O_CLOEXEC);
        if (fd >= 0) {
            unlink(tmp_path.c_str());
        }
        else {
            return { nullptr, nullptr };
        }
        if (ftruncate(fd, 4 * cx * cy) < 0) {
            close(fd);
            return { nullptr, nullptr };
        }
        auto data = (uint32_t*) mmap(nullptr, 4 * cx * cy, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (data == MAP_FAILED) {
            close(fd);
            return { nullptr, nullptr };
        }
        auto pool = attach_unique(wl_shm_create_pool(shm, fd, 4 * cx * cy));
        auto buff = attach_unique(wl_shm_pool_create_buffer(pool.get(),
                                                            0,
                                                            cx, cy,
                                                            cx * 4,
                                                            WL_SHM_FORMAT_ARGB8888));
        pool = nullptr;
        return std::make_pair(std::move(buff), data);
    };
    int cx = 1920;
    int cy = 1080;
    auto [ buff, pixel ] = create_buffer(shm.get(), cx, cy);
    {
        static constexpr wl_buffer_listener listener {
            .release = [](auto...) noexcept {
                std::cerr << "***** buffer released." << std::endl;
            },
        };
    }
    for (int i = 0; i < cx * cy; ++i) {
        *pixel++ = 0x800000ff;
    }
    wl_surface_frame(surface.get());
    wl_surface_attach(surface.get(), buff.get(), 0, 0);
    wl_surface_commit(surface.get());
    wl_display_flush(display.get());
    while (wl_display_dispatch(display.get()) != -1) {
        if (1 == key_input) {
            break;
        }
    }
    return 0;
}
