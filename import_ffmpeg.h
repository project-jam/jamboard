#pragma once
#include <string>

bool ffmpeg_convert_audio(const std::string& src, const std::string& dst);
bool ffmpeg_extract_thumbnail(const std::string& src, const std::string& dst);
