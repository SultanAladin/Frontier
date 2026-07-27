/*==============================================================================================================================================
                                                            IMGUIPLATFORMRELAY.H
==============================================================================================================================================*/
// 🧩 The ImGui OS-half attach point. ImGui's Vulkan half (imgui_impl_vulkan) is platform-neutral, but its platform half needs the raw OS
//    messages — mouse enter / leave, click, wheel, key, char, focus — which the native PlatformWindow owns. This wrapper relays the two: on
//    attach it stands up the per-OS ImGui platform backend (Win32: ImGui_ImplWin32_Init on the window's native handle) and installs a message
//    observer on the window that forwards every OS message into ImGui's own handler; each frame Advance pumps the backend's new-frame; on detach
//    it removes the observer and shuts the backend down. Only this one .cpp names an OS-specific ImGui backend, so swapping platforms (a future
//    Linux relay fed from InputSnapshot) is a change here alone. The camera still reads its delta straight off PlatformWindow.Input — this
//    observer only feeds ImGui, it never intercepts the raw-input motion stream the viewport navigation depends on.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_IMGUIPLATFORMRELAY_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_IMGUIPLATFORMRELAY_H

namespace Frontier
{

struct PlatformWindow;

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Stand up ImGui's OS-platform backend for this window and install the message observer that feeds it. Call once, after the
// ImGui context exists and before the first frame. Returns false on an unsupported platform (the stub floor), true otherwise.
bool AttachImguiPlatform(PlatformWindow& Window);

// Advance ImGui's OS-platform backend one frame (mouse position, focus, cursor). Call before ImGui_ImplVulkan_NewFrame each frame.
void AdvanceImguiPlatform();

// Remove the message observer and shut ImGui's OS-platform backend down. Call once during teardown, before ImGui::DestroyContext.
void DetachImguiPlatform(PlatformWindow& Window);

}   // namespace Frontier

#endif
