CXX = g++
CXXFLAGS = -I/ucrt64/include -static-libgcc -static-libstdc++ -mwindows
LDFLAGS = -L/ucrt64/lib
LIBS_STATIC = -lglfw3 -lopengl32 -lgdi32
LIBS_DYNAMIC = -lavformat -lavcodec -lavutil -lswresample -lswscale
SOURCES = main.cpp audio_engine.cpp ui.cpp config.cpp import.cpp import_ffmpeg.cpp engine_globals.cpp imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui_impl_glfw.cpp imgui_impl_opengl3.cpp
TARGET = jamboard.exe

.PHONY: all dlls clean

all: $(TARGET) dlls

$(TARGET): $(SOURCES)
	$(CXX) $(SOURCES) -o $(TARGET) $(CXXFLAGS) $(LDFLAGS) -Wl,-Bstatic $(LIBS_STATIC) -Wl,-Bdynamic $(LIBS_DYNAMIC)

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

clean:
	rm -f $(TARGET) *.dll
