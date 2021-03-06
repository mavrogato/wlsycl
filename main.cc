
#include <iostream>
#include <bitset>
#include <string_view>

#include <CL/sycl.hpp>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "wayland-client-helper.hpp"

int main() {
    static auto xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    auto display = attach_unique(wl_display_connect(nullptr));
    auto [compositor, shell, shm, seat] = register_global<wl_compositor,
                                                          wl_shell,
                                                          wl_shm,
                                                          wl_seat>(display.get());
    static constexpr auto required_shm_format = WL_SHM_FORMAT_ARGB8888;
    bool required_shm_format_supported = false;
    {
        static constexpr wl_shm_listener listener {
            .format = [](void* data, auto, uint32_t format) noexcept {
                if (required_shm_format == format) {
                    *reinterpret_cast<bool*>(data) = true;
                }
                std::cout << "supported format: " << std::bitset<32>(format) << std::endl;
            },
        };
        if (0 != wl_shm_add_listener(shm.get(), &listener, &required_shm_format_supported)) {
            std::cerr << "wl_shm_add_listener failed..." << std::endl;
            return -1;
        }
    }
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
    wl_display_roundtrip(display.get());
    assert(required_shm_format_supported);
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
    constexpr int cx = 1920;
    constexpr int cy = 1080;
    auto [ buff, pixel ] = create_buffer(shm.get(), cx, cy);
    {
        static constexpr wl_buffer_listener listener {
            .release = [](auto...) noexcept {
                std::cerr << "***** buffer released." << std::endl;
            },
        };
	wl_buffer_add_listener(buff.get(), &listener, nullptr);
    }
    // for (int i = 0; i < cx * cy; ++i) {
    //     *pixel++ = 0x800000ff;
    // }
    {
        //auto pixel2 = new uint32_t[cx * cy];
        sycl::range<1> r(cx * cy);
        sycl::buffer d(pixel, r);
        sycl::queue queue;
        queue.submit([&](sycl::handler& h) {
            auto a = d.get_access<sycl::access::mode::write>(h);
            h.parallel_for(r, [=](sycl::id<> idx) {
                a[idx] = 0x4000ff00;
            });
        });
        queue.wait();
        sycl::host_accessor D(d, sycl::read_only);
        std::cout << std::bitset<32>(pixel[0]) << std::endl;
        // for (int i = 0; i < cx*cy; ++i) {
        //     *pixel++ = D[i];
        // }
        // cl::sycl::buffer d(pixel, cl::sycl::range<2>(cx, cy));
        // cl::sycl::queue queue;
        // queue.submit([&](cl::sycl::handler& cgh) {
        //     auto a = d.get_access<cl::sycl::access::mode::read_write>(cgh);
        //     cgh.parallel_for<class simple_test>(
        //         cl::sycl::range<2>(cx, cy),
        //         [=](cl::sycl::id<2> idx) {
        //             a[idx] = 0xff00ff00;
        //         });
        // });
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
