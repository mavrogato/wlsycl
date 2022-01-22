#ifndef INCLUDE_WAYLAND_CLIENT_HELPER_HPP_D18E43DE_B3A0_450D_8DD7_5BF6F8542492
#define INCLUDE_WAYLAND_CLIENT_HELPER_HPP_D18E43DE_B3A0_450D_8DD7_5BF6F8542492

#include <concepts>
#include <iosfwd>
#include <tuple>
#include <memory>
#include <cassert>
#include <string_view>

#include <wayland-client.h>

/////////////////////////////////////////////////////////////////////////////
inline namespace wayland_client_helper
{

template <class> constexpr void* wl_interface_ptr = nullptr;
#define INTERN_WL_INTERFACE(wl_client)                                  \
    template <> constexpr wl_interface const* wl_interface_ptr<wl_client> = &wl_client##_interface;
INTERN_WL_INTERFACE(wl_display);
INTERN_WL_INTERFACE(wl_registry);
INTERN_WL_INTERFACE(wl_compositor);
INTERN_WL_INTERFACE(wl_shell);
INTERN_WL_INTERFACE(wl_seat);
INTERN_WL_INTERFACE(wl_keyboard);
INTERN_WL_INTERFACE(wl_pointer);
INTERN_WL_INTERFACE(wl_touch);
INTERN_WL_INTERFACE(wl_shm);
INTERN_WL_INTERFACE(wl_surface);
INTERN_WL_INTERFACE(wl_shell_surface);
INTERN_WL_INTERFACE(wl_buffer);
INTERN_WL_INTERFACE(wl_shm_pool);
#undef INTERN_WL_INTERFACE

template <class T>
concept wl_client_t = std::same_as<decltype (wl_interface_ptr<T>), wl_interface const* const>;

template <wl_client_t T, class Ch>
auto& operator << (std::basic_ostream<Ch>& output, T const* ptr) noexcept {
    return output << static_cast<void const*>(ptr)
                  << '['
                  << wl_interface_ptr<T>->name
                  << ']';
}

template <wl_client_t T>
auto attach_unique(T* ptr) noexcept {
    assert(ptr);
    static void (*deleter)(T*) = nullptr;
    if      constexpr (&wl_display_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_display_disconnect;
                      }
    else if constexpr (&wl_registry_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_registry_destroy;
                      }
    else if constexpr (&wl_compositor_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_compositor_destroy;
                      }
    else if constexpr (&wl_shell_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_shell_destroy;
                      }
    else if constexpr (&wl_seat_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_seat_destroy;
                      }
    else if constexpr (&wl_keyboard_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_keyboard_release;
                      }
    else if constexpr (&wl_pointer_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_pointer_release;
                      }
    else if constexpr (&wl_touch_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_touch_release;
                      }
    else if constexpr (&wl_shm_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_shm_destroy;
                      }
    else if constexpr (&wl_surface_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_surface_destroy;
                      }
    else if constexpr (&wl_shell_surface_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_shell_surface_destroy;
                      }
    else if constexpr (&wl_buffer_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_buffer_destroy;
                      }
    else if constexpr (&wl_shm_pool_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_shm_pool_destroy;
                      }
    else {
        //!!! static_assert(!"no deleter");
    }
    auto deleter_closure = [](T* ptr) noexcept {
        std::cout << wl_interface_ptr<T>->name << " deleting..." << std::endl; //!!!
        deleter(ptr);
    };
    return std::unique_ptr<T, decltype (deleter_closure)>(ptr, deleter_closure);
}

template <size_t N>
void register_global_callback(auto...) noexcept { }

template <size_t N, wl_client_t First, wl_client_t... Rest>
void register_global_callback(auto... args) noexcept
{
    auto [data, registry, id, interface, version] = std::tuple(args...);
    //!!! if constexpr (N == 0) { std::cout << interface << " (ver." << version << ") found."; }
    constexpr auto interface_ptr = wl_interface_ptr<First>;
    auto& ret = *(reinterpret_cast<First**>(data) + N);
    if (std::string_view(interface) == interface_ptr->name) {
        ret = (First*) wl_registry_bind(registry,
                                         id,
                                         interface_ptr,
                                         version);
        //!!! std::cout << "  ==> registered at " << ret;
    }
    else {
        register_global_callback<N-1, Rest...>(args...);
    }
    //!!! if constexpr (N == 0) { std::cout << std::endl; }
}

/////////////////////////////////////////////////////////////////////////////
namespace tentative_solution
{
template <class Tuple, size_t... I>
auto transform_each_impl(Tuple t, std::index_sequence<I...>) noexcept {
    return std::make_tuple(
        attach_unique(std::get<I>(t))...
    );
}
template <class... Args>
auto transform_each(std::tuple<Args...> const& t) noexcept {
    return transform_each_impl(t, std::make_index_sequence<sizeof... (Args)>{});
}
} // end of namespace tentative_solution

// Note: Still depends the reversed memory order of std::tuple
template <class... Args>
auto register_global(wl_display* display) noexcept {
    auto registry = attach_unique(wl_display_get_registry(display));
    std::tuple<Args*...> result;
    static constexpr wl_registry_listener listener {
        .global = register_global_callback<sizeof... (Args) -1, Args...>,
        .global_remove = [](auto...) noexcept { },
    };
    wl_registry_add_listener(registry.get(), &listener, &result);
    //wl_display_dispatch(display);
    wl_display_roundtrip(display);
    return tentative_solution::transform_each(result);
}

} // end of namespace wayland_client_helper

#endif/*INCLUDE_WAYLAND_CLIENT_HELPER_HPP_D18E43DE_B3A0_450D_8DD7_5BF6F8542492*/
