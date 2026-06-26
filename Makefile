CXX = g++
CXXFLAGS = -I/ucrt64/include -static-libgcc -static-libstdc++ -mwindows -DJAMBOARD_VERSION="\"v4.1\"" -DCPPHTTPLIB_OPENSSL_SUPPORT
LDFLAGS = -L/ucrt64/lib
LIBS_STATIC = -lglfw3 -lopengl32 -lgdi32 -lcomdlg32 -lshell32 -lws2_32 -lcrypt32 -lbcrypt -lwinmm
LIBS_DYNAMIC = -lavformat -lavcodec -lavutil -lswresample -lswscale -lssl -lcrypto
OBJDIR = build
OBJS = $(OBJDIR)/main.o $(OBJDIR)/audio_engine.o $(OBJDIR)/ui.o $(OBJDIR)/config.o $(OBJDIR)/import.o $(OBJDIR)/import_ffmpeg.o $(OBJDIR)/engine_globals.o $(OBJDIR)/update_checker.o $(OBJDIR)/myinstants.o $(OBJDIR)/imgui.o $(OBJDIR)/imgui_draw.o $(OBJDIR)/imgui_tables.o $(OBJDIR)/imgui_widgets.o $(OBJDIR)/imgui_impl_glfw.o $(OBJDIR)/imgui_impl_opengl3.o
TARGET = jamboard.exe

.PHONY: all dlls deps clean

all: $(TARGET) dlls deps

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(CXXFLAGS) $(LDFLAGS) -Wl,-Bstatic $(LIBS_STATIC) -Wl,-Bdynamic $(LIBS_DYNAMIC)

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	$(CXX) -c $< -o $@ $(CXXFLAGS)

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Headers as dependencies — touching a header rebuilds everything that includes it
$(OBJDIR)/main.o: main.cpp audio_engine.h config.h ui.h import.h update_checker.h data_models.h
$(OBJDIR)/audio_engine.o: audio_engine.cpp audio_engine.h config.h data_models.h
$(OBJDIR)/ui.o: ui.cpp ui.h audio_engine.h import_ffmpeg.h import.h config.h update_checker.h myinstants.h data_models.h
$(OBJDIR)/config.o: config.cpp config.h data_models.h json.hpp
$(OBJDIR)/import.o: import.cpp import.h import_ffmpeg.h data_models.h
$(OBJDIR)/import_ffmpeg.o: import_ffmpeg.cpp import_ffmpeg.h
$(OBJDIR)/engine_globals.o: engine_globals.cpp audio_engine.h data_models.h
$(OBJDIR)/update_checker.o: update_checker.cpp update_checker.h httplib.h json.hpp
$(OBJDIR)/myinstants.o: myinstants.cpp myinstants.h httplib.h import_ffmpeg.h

# ImGui files rarely change — compile once
$(OBJDIR)/imgui.o: imgui.cpp imgui.h
$(OBJDIR)/imgui_draw.o: imgui_draw.cpp imgui.h
$(OBJDIR)/imgui_tables.o: imgui_tables.cpp imgui.h
$(OBJDIR)/imgui_widgets.o: imgui_widgets.cpp imgui.h
$(OBJDIR)/imgui_impl_glfw.o: imgui_impl_glfw.cpp imgui.h imgui_impl_glfw.h
$(OBJDIR)/imgui_impl_opengl3.o: imgui_impl_opengl3.cpp imgui.h imgui_impl_opengl3.h

dlls: $(TARGET)
	@echo "Collecting runtime DLLs..."
	@for dll in $$(ldd $(TARGET) 2>/dev/null | grep '/ucrt64/' | awk '{print $$3}' | sort -u); do \
		cp -n "$$dll" . 2>/dev/null; \
	done
	@for round in 1 2 3; do \
		for dll in *.dll; do \
			if [ -f "$$dll" ]; then \
				for dep in $$(ldd "$$dll" 2>/dev/null | grep '/ucrt64/' | awk '{print $$3}'); do \
					cp -n "$$dep" . 2>/dev/null; \
				done; \
			fi; \
		done; \
	done
	@echo "Done."

deps:
	@echo "Downloading yt-dlp.exe..."
	@curl -L -o yt-dlp.exe https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe
	@echo "Done."

clean:
	rm -rf $(OBJDIR) $(TARGET) *.dll
