# ============================================================
# JamBoard - cross-platform build
# ============================================================

CXX      ?= g++

# Use ccache when it is installed. It makes rebuilds of unchanged
# files almost instant, which matters a lot on this project.
CCACHE   := $(shell command -v ccache 2>/dev/null)
ifneq ($(CCACHE),)
    CXX := $(CCACHE) $(CXX)
endif
# Single source of truth for the version. The update checker compares
# against the same value, so the app can never disagree with the package.
VERSION  := $(shell cat VERSION 2>/dev/null || echo 4.2)
PKGNAME  := jamboard

CXXFLAGS := -DJAMBOARD_VERSION="\"v$(VERSION)\"" -DCPPHTTPLIB_OPENSSL_SUPPORT -std=c++17
LDFLAGS  :=
LIBS_DYNAMIC := -lavformat -lavcodec -lavutil -lswresample -lswscale -lssl -lcrypto

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
    TARGET  := jamboard
    OBJDIR  := build/linux
    YTDLP   := yt-dlp
    YTDLP_URL := https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp
    CXXFLAGS += -static-libgcc -static-libstdc++
    LIBS = -lglfw -lGL -lpthread -ldl -lm -lX11 -lXrandr -lXinerama -lXcursor -lXi \
           -lavformat -lavcodec -lavutil -lswresample -lswscale -lssl -lcrypto
    PKG_BUILD_DEPS := debhelper dh-make debhelper-compat
else ifeq ($(UNAME_S),Darwin)
    TARGET  := jamboard
    OBJDIR  := build/macos
    YTDLP   := yt-dlp
    YTDLP_URL := https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos
    LIBS = -lglfw -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
else
    # Windows / MSYS2
    TARGET  := jamboard.exe
    OBJDIR  := build/windows
    YTDLP   := yt-dlp.exe
    YTDLP_URL := https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe
    CXXFLAGS += -I/ucrt64/include -static-libgcc -static-libstdc++ -mwindows
    LDFLAGS += -L/ucrt64/lib
    LIBS_STATIC = -lglfw3 -lopengl32 -lgdi32 -lcomdlg32 -lshell32 -lws2_32 -lcrypt32 -lbcrypt -lwinmm
endif

OBJS := $(OBJDIR)/main.o $(OBJDIR)/audio_engine.o $(OBJDIR)/ui.o $(OBJDIR)/config.o \
        $(OBJDIR)/import.o $(OBJDIR)/import_ffmpeg.o $(OBJDIR)/engine_globals.o \
        $(OBJDIR)/update_checker.o $(OBJDIR)/myinstants.o $(OBJDIR)/tts.o \
        $(OBJDIR)/acronyms.o \
        $(OBJDIR)/imgui.o $(OBJDIR)/imgui_draw.o $(OBJDIR)/imgui_tables.o $(OBJDIR)/imgui_widgets.o \
        $(OBJDIR)/imgui_impl_glfw.o $(OBJDIR)/imgui_impl_opengl3.o

# ------------------------------------------------------------
# Content-hash based rebuild detection.
#
# mtime-based dependencies are unreliable on network/NTFS/exFAT
# mounts (and on drives shared between Windows and Linux) because
# overwriting a file does not always update its modification time.
# Hashing the sources sidesteps that entirely: when any source
# changes the stamp name changes, the stamp is rebuilt, and every
# object becomes older than the stamp and is recompiled.
# ------------------------------------------------------------
SOURCES := $(wildcard *.cpp) $(wildcard *.h) \
           $(wildcard install/*.iss) $(wildcard packaging/*/PKGBUILD) \
           $(wildcard packaging/*.spec) $(wildcard packaging/debian/*)
SRC_HASH := $(shell cat $(SOURCES) 2>/dev/null | sha256sum | cut -c1-16)
STAMP    := $(OBJDIR)/.src-$(SRC_HASH)

.PHONY: all dlls deps deps-force clean rebuild install deb rpm aur tarball \
        dist help package

all: $(TARGET) dlls

$(TARGET): $(OBJS)
ifeq ($(UNAME_S),Linux)
	$(CXX) $(OBJS) -o $(TARGET) $(CXXFLAGS) $(LDFLAGS) $(LIBS)
else ifeq ($(UNAME_S),Darwin)
	$(CXX) $(OBJS) -o $(TARGET) $(CXXFLAGS) $(LDFLAGS) $(LIBS)
else
	$(CXX) $(OBJS) -o $(TARGET) $(CXXFLAGS) $(LDFLAGS) -Wl,-Bstatic $(LIBS_STATIC) -Wl,-Bdynamic $(LIBS_DYNAMIC)
endif

$(OBJDIR)/%.o: %.cpp $(STAMP) | $(OBJDIR)
	$(CXX) -c $< -o $@ $(CXXFLAGS)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(STAMP):
	@mkdir -p $(OBJDIR)
	@rm -f $(OBJDIR)/.src-* 2>/dev/null || true
	@touch $@

dlls:
ifeq ($(UNAME_S),Linux)
	@echo "No DLLs needed on Linux."
else ifeq ($(UNAME_S),Darwin)
	@echo "No DLLs needed on macOS."
else
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
endif

# yt-dlp is only needed for the "download from URL" feature.
# It is optional on Linux: a system-wide `yt-dlp` is used if present.
deps:
	@if [ -x "$(YTDLP)" ]; then \
		echo "$(YTDLP) already present - skipping download."; \
	else \
		echo "Downloading $(YTDLP)..."; \
		curl -L --fail -o "$(YTDLP)" "$(YTDLP_URL)" && chmod +x "$(YTDLP)" \
			|| echo "WARNING: could not download yt-dlp. URL downloads will be disabled."; \
	fi

deps-force:
	@rm -f $(YTDLP)
	@$(MAKE) deps

# Force a full rebuild (needed on filesystems with unreliable mtimes)
# Recursive make keeps clean and all ordered even with -j.
rebuild:
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory all

clean:
	rm -rf $(OBJDIR) $(TARGET) jamboard.exe *.dll

# ============================================================
# Packaging
# ============================================================

DIST_DIR := dist

# --- .deb (Debian/Ubuntu) ---
deb:
	@command -v dpkg-buildpackage >/dev/null 2>&1 || { \
		echo "ERROR: dpkg-buildpackage not found. Install: sudo apt install devscripts debhelper"; \
		exit 1; }
	@$(MAKE) --no-print-directory $(TARGET)
	@rm -rf $(OBJDIR)/deb
	@mkdir -p $(OBJDIR)/deb
	@cp -r packaging/debian $(OBJDIR)/deb/debian
	@cp $(TARGET) $(OBJDIR)/deb/jamboard
	@cp packaging/linux/jamboard.desktop $(OBJDIR)/deb/jamboard.desktop
	@cp packaging/linux/jamboard.svg $(OBJDIR)/deb/jamboard.svg
	@cp LICENSE $(OBJDIR)/deb/LICENSE
	@cp zpix.ttf $(OBJDIR)/deb/zpix.ttf
	@chmod +x $(OBJDIR)/deb/debian/rules $(OBJDIR)/deb/debian/postinst
	@cd $(OBJDIR)/deb && dpkg-buildpackage -b -us -uc
	@mkdir -p $(DIST_DIR)
	@mv -f $(OBJDIR)/deb/../*.deb $(DIST_DIR)/ 2>/dev/null || \
		mv -f $(CURDIR)/../*.deb $(DIST_DIR)/ 2>/dev/null || true
	@echo "Built: $(DIST_DIR)/*.deb"

# --- AUR tarball + PKGBUILD ---
aur: tarball
	@mkdir -p $(DIST_DIR)/aur
	@cp packaging/aur/PKGBUILD $(DIST_DIR)/aur/PKGBUILD
	@cp $(DIST_DIR)/$(PKGNAME)-$(VERSION)-linux-x86_64.tar.gz $(DIST_DIR)/aur/
	@cd $(DIST_DIR)/aur && \
		HASH=$$(sha256sum $(PKGNAME)-$(VERSION)-linux-x86_64.tar.gz | awk '{print $$1}') && \
		sha256sum $(PKGNAME)-$(VERSION)-linux-x86_64.tar.gz > PKGBUILD.sha256 && \
		sed -i "s@sha256sums=('SKIP')@sha256sums=('$$HASH')@" PKGBUILD
	@echo "AUR package staged in $(DIST_DIR)/aur"

# --- .rpm (Fedora/RHEL/openSUSE) ---
rpm: tarball
	@command -v rpmbuild >/dev/null 2>&1 || { \
		echo "ERROR: rpmbuild not found. Install: sudo dnf install rpm-build"; \
		exit 1; }
	@mkdir -p $(DIST_DIR)
	@rm -rf /tmp/jamboard-rpmbuild && mkdir -p /tmp/jamboard-rpmbuild/SOURCES /tmp/jamboard-rpmbuild/SPECS
	@cp $(DIST_DIR)/$(PKGNAME)-$(VERSION)-linux-x86_64.tar.gz /tmp/jamboard-rpmbuild/SOURCES/
	@cp LICENSE /tmp/jamboard-rpmbuild/SOURCES/
	@cp zpix.ttf /tmp/jamboard-rpmbuild/SOURCES/
	@cp packaging/jamboard.spec /tmp/jamboard-rpmbuild/SPECS/
	@rpmbuild -bb --define "_topdir /tmp/jamboard-rpmbuild" \
		--define "_sourcedir /tmp/jamboard-rpmbuild/SOURCES" \
		--define "_rpmdir $(CURDIR)/$(DIST_DIR)" \
		--define "_builddir /tmp/jamboard-rpmbuild/BUILD" \
		--define "_srcrpmdir $(CURDIR)/$(DIST_DIR)" \
		--nodeps \
		/tmp/jamboard-rpmbuild/SPECS/jamboard.spec
	@mv -f $(DIST_DIR)/x86_64/*.rpm $(DIST_DIR)/ 2>/dev/null || true
	@rm -rf $(DIST_DIR)/x86_64 /tmp/jamboard-rpmbuild
	@echo "Built: $(DIST_DIR)/*.rpm"

# --- Portable tarball (for AUR / GitHub releases) ---
tarball: $(TARGET)
	@mkdir -p $(DIST_DIR)/$(PKGNAME)-$(VERSION)/sounds
	@cp $(TARGET) $(DIST_DIR)/$(PKGNAME)-$(VERSION)/
	@cp packaging/linux/jamboard.desktop $(DIST_DIR)/$(PKGNAME)-$(VERSION)/ 2>/dev/null || true
	@if [ -f packaging/linux/jamboard.svg ]; then \
		cp packaging/linux/jamboard.svg $(DIST_DIR)/$(PKGNAME)-$(VERSION)/; fi
	@cp LICENSE $(DIST_DIR)/$(PKGNAME)-$(VERSION)/ 2>/dev/null || true
	@cp zpix.ttf $(DIST_DIR)/$(PKGNAME)-$(VERSION)/ 2>/dev/null || true
	@tar -czf $(DIST_DIR)/$(PKGNAME)-$(VERSION)-linux-x86_64.tar.gz -C $(DIST_DIR) $(PKGNAME)-$(VERSION)
	@rm -rf $(DIST_DIR)/$(PKGNAME)-$(VERSION)
	@echo "Built: $(DIST_DIR)/$(PKGNAME)-$(VERSION)-linux-x86_64.tar.gz"

# Build every distributable artifact
package: tarball deb rpm

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)/usr/bin/jamboard
	install -Dm644 packaging/linux/jamboard.desktop \
		$(DESTDIR)/usr/share/applications/jamboard.desktop
	install -Dm644 packaging/linux/jamboard.svg \
		$(DESTDIR)/usr/share/icons/hicolor/scalable/apps/jamboard.svg

help:
	@echo "JamBoard $(VERSION) build targets:"
	@echo "  make            - build the app (Linux/macOS/Windows)"
	@echo "  make rebuild    - clean rebuild (use on odd filesystems)"
	@echo "  make deps       - download yt-dlp (optional on Linux)"
	@echo "  make deb        - build a .deb package"
	@echo "  make rpm        - build an .rpm package"
	@echo "  make aur        - stage an AUR package (tarball + PKGBUILD)"
	@echo "  make tarball    - build a portable linux tarball"
	@echo "  make package    - build deb + rpm + tarball"
	@echo "  make install    - install to DESTDIR (default /)"
