/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "RmlUi_Backend.h"
#include "RmlUi_Platform_SDL.h"
#include "RmlUi_Renderer_SDL.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Debugger/Debugger.h>
#include <SDL.h>

static SDL_Renderer* renderer = nullptr;

static Rml::Context* context = nullptr;
static int window_width = 0;
static int window_height = 0;
static bool running = false;

static Rml::UniquePtr<RenderInterface_SDL> render_interface;
static Rml::UniquePtr<SystemInterface_SDL> system_interface;

static void ProcessKeyDown(SDL_Event& event, Rml::Input::KeyIdentifier key_identifier, const int key_modifier_state);

static void UpdateWindowDimensions(int width = 0, int height = 0)
{
	if (width > 0)
		window_width = width;
	if (height > 0)
		window_height = height;
	if (context)
		context->SetDimensions(Rml::Vector2i(window_width, window_height));
}

bool Backend::InitializeInterfaces()
{
	RMLUI_ASSERT(!system_interface && !render_interface);

	system_interface = Rml::MakeUnique<SystemInterface_SDL>();
	Rml::SetSystemInterface(system_interface.get());

	render_interface = Rml::MakeUnique<RenderInterface_SDL>();
	Rml::SetRenderInterface(render_interface.get());

	return true;
}

void Backend::ShutdownInterfaces()
{
	render_interface.reset();
	system_interface.reset();
}

bool Backend::OpenWindow(const char* name, int width, int height, bool allow_resize)
{
	if (!RmlSDL::Initialize())
		return false;

	const Uint32 window_flags = 0;
	SDL_Window* window = nullptr;
	if (!RmlSDL::CreateWindow(name, width, height, allow_resize, window_flags, window))
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "SDL error on create window: %s\n", SDL_GetError());
		return false;
	}

	/*
	 * Force a specific back-end
	SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d");
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
	*/

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer)
		return false;

	SDL_RendererInfo renderer_info;
	if (SDL_GetRendererInfo(renderer, &renderer_info) == 0)
	{
		Rml::Log::Message(Rml::Log::LT_INFO, "Using SDL renderer: %s\n", renderer_info.name);
	}

	RmlSDLrenderer::Initialize(renderer);
	UpdateWindowDimensions(width, height);

	return true;
}

void Backend::CloseWindow()
{
	RmlSDLrenderer::Shutdown();

	SDL_DestroyRenderer(renderer);

	renderer = nullptr;

	RmlSDL::CloseWindow();
	RmlSDL::Shutdown();
}

void Backend::EventLoop(ShellIdleFunction idle_function)
{
	running = true;

	while (running)
	{
		SDL_Event event;

		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_QUIT:
				running = false;
				break;
			case SDL_KEYDOWN:
				// Intercept keydown events to handle global sample shortcuts.
				ProcessKeyDown(event, RmlSDL::ConvertKey(event.key.keysym.sym), RmlSDL::GetKeyModifierState());
				break;
			case SDL_WINDOWEVENT:
				switch (event.window.event)
				{
				case SDL_WINDOWEVENT_SIZE_CHANGED:
					UpdateWindowDimensions(event.window.data1, event.window.data2);
					break;
				}
				break;
			default:
				RmlSDL::EventHandler(event);
				break;
			}
		}

		idle_function();
	}
}

void Backend::RequestExit()
{
	running = false;
}

void Backend::BeginFrame()
{
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);

	RmlSDLrenderer::BeginFrame();
}

void Backend::PresentFrame()
{
	RmlSDLrenderer::EndFrame();

	SDL_RenderPresent(renderer);
}

void Backend::SetContext(Rml::Context* new_context)
{
	context = new_context;
	RmlSDL::SetContextForInput(new_context);
	UpdateWindowDimensions();
}

static void ProcessKeyDown(SDL_Event& event, Rml::Input::KeyIdentifier key_identifier, const int key_modifier_state)
{
	if (!context)
		return;

	// Toggle debugger and set dp-ratio using Ctrl +/-/0 keys. These global shortcuts take priority.
	if (key_identifier == Rml::Input::KI_F8)
	{
		Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
	}
	else if (key_identifier == Rml::Input::KI_0 && key_modifier_state & Rml::Input::KM_CTRL)
	{
		context->SetDensityIndependentPixelRatio(1.f);
	}
	else if (key_identifier == Rml::Input::KI_1 && key_modifier_state & Rml::Input::KM_CTRL)
	{
		context->SetDensityIndependentPixelRatio(1.f);
	}
	else if (key_identifier == Rml::Input::KI_OEM_MINUS && key_modifier_state & Rml::Input::KM_CTRL)
	{
		const float new_dp_ratio = Rml::Math::Max(context->GetDensityIndependentPixelRatio() / 1.2f, 0.5f);
		context->SetDensityIndependentPixelRatio(new_dp_ratio);
	}
	else if (key_identifier == Rml::Input::KI_OEM_PLUS && key_modifier_state & Rml::Input::KM_CTRL)
	{
		const float new_dp_ratio = Rml::Math::Min(context->GetDensityIndependentPixelRatio() * 1.2f, 2.5f);
		context->SetDensityIndependentPixelRatio(new_dp_ratio);
	}
	else
	{
		// No global shortcuts detected, submit the key to platform handler.
		if (RmlSDL::EventHandler(event))
		{
			// The key was not consumed, check for shortcuts that are of lower priority.
			if (key_identifier == Rml::Input::KI_R && key_modifier_state & Rml::Input::KM_CTRL)
			{
				for (int i = 0; i < context->GetNumDocuments(); i++)
				{
					Rml::ElementDocument* document = context->GetDocument(i);
					const Rml::String& src = document->GetSourceURL();
					if (src.size() > 4 && src.substr(src.size() - 4) == ".rml")
					{
						document->ReloadStyleSheet();
					}
				}
			}
		}
	}
}
