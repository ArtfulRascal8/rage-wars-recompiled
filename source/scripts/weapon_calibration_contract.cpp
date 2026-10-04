#define NOMINMAX
#include "rage_wars_weapon_calibration.hpp"
#include <cassert>
#include <limits>
#include <iostream>
using namespace xr64::rage_wars::recomp;
int main(){
 auto root=std::filesystem::temp_directory_path()/("xr64-calibration-"+std::to_string(GetCurrentProcessId()));
 std::filesystem::create_directories(root);_putenv_s("XR64_PORT_OPTIONS_CONFIG",(root/"options.json").string().c_str());
 _putenv_s("XR64_WEAPON_ASSETS","");_putenv_s("XR64_RW_PREVIEW_WEAPONS","");
 assert(weapon_profile_directory()==root);
 assert(weapon_asset_directory()==root/"weapon-previews");
 std::filesystem::create_directories(root/"runtime/preview-weapons");
 assert(weapon_asset_directory()==root/"runtime/preview-weapons");
 std::filesystem::create_directories(root/"weapon-previews");
 assert(weapon_asset_directory()==root/"weapon-previews");
 _putenv_s("XR64_RW_PREVIEW_WEAPONS",(root/"legacy-models").string().c_str());
 assert(weapon_asset_directory()==root/"legacy-models");
 _putenv_s("XR64_WEAPON_ASSETS",(root/"chosen-models").string().c_str());
 assert(weapon_asset_directory()==root/"chosen-models");
 _putenv_s("XR64_WEAPON_ASSETS","");_putenv_s("XR64_RW_PREVIEW_WEAPONS","");
 WeaponCalibration c;assert(valid_weapon_calibration(c));assert(load_weapon_calibration(4).scale==1);
 auto p=calibrated_weapon_point({1,2,3},c);assert((p==std::array<float,3>{1,2,3}));
 c.offset={0.1F,-0.2F,0.3F};c.degrees={0,90,0};c.scale=0.5F;
 p=calibrated_weapon_point({0,0,-1},c);assert(std::abs(p[0]+0.4F)<0.00001F&&std::abs(p[1]+0.2F)<0.00001F&&std::abs(p[2]-0.3F)<0.00001F);
 std::string error;assert(save_weapon_calibration(4,c,error));auto loaded=load_weapon_calibration(4);assert(loaded.offset==c.offset&&loaded.degrees==c.degrees&&loaded.scale==c.scale);
 assert(load_weapon_calibration(6).scale==1);c.scale=std::numeric_limits<float>::quiet_NaN();assert(!save_weapon_calibration(4,c,error));assert(load_weapon_calibration(4).scale==0.5F);
 {std::ofstream f(weapon_calibration_path(4));f<<"1 0 0 0 0 0 0 999";}assert(load_weapon_calibration(4).scale==1);
 assert(!save_weapon_calibration(15,{},error));assert(!save_weapon_calibration(0,{},error));
 std::filesystem::remove_all(root);std::cout<<"PASS calibration identity, transform, persistence, weapon isolation, malformed data, failed-save preservation\n";
}
