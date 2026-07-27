#pragma once

// Dear ImGui as an eacp View. Include this and nothing else.
//
//     struct MyApp
//     {
//         MyApp()
//         {
//             view.onDraw = [] { ImGui::ShowDemoWindow(); };
//             window.setContentView(view);
//         }
//
//         eacp::Gui::ImGuiView view;
//         eacp::Graphics::Window window;
//     };
//
//     int main() { return eacp::Apps::run<MyApp>(); }

#include "Input/KeyMap.h"
#include "Renderer/DrawRenderer.h"
#include "View/ImGuiView.h"
