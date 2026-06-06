#pragma once
#include <string>
void load_config_from_json();
void save_config_to_json();
void rename_profile_key(const std::string& old_key, const std::string& new_key);
void copy_profile_key(const std::string& src_key, const std::string& dest_key);
void rekey_profiles_folder(const std::string& old_folder, const std::string& new_folder);
void copy_profiles_folder(const std::string& src_folder, const std::string& dest_folder);
void remove_profiles_folder(const std::string& folder);
