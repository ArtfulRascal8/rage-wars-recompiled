// Included inside the renderer namespace: runs before the guest runtime starts.
bool RageWarsDesktopRenderer::run_weapon_calibration(std::string& error) {
#ifdef XR64_OPENXR
    if(!initialize(error,true))return false;
    auto* window=static_cast<SDL_Window*>(window_);
    if(!xr_enabled()){error=xr_transition_status();shutdown();return false;}
    if(g_sdl.gl_make_current(window,static_cast<SDL_GLContext>(context_))!=0){error=sdl_error();shutdown();return false;}
    set_mouse_capture(false);
    const GLuint font=glGenLists(96);
    HFONT typeface=CreateFontA(-22,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FIXED_PITCH,"Consolas");
    HDC dc=wglGetCurrentDC();auto old=SelectObject(dc,typeface);
    const bool font_ok=font&&wglUseFontBitmapsA(dc,32,96,font);
    SelectObject(dc,old);DeleteObject(typeface);
    if(!font_ok){if(font)glDeleteLists(font,96);error="Cannot create calibration labels.";shutdown();return false;}
    unsigned weapon=4,field=0;WeaponCalibration calibration=load_weapon_calibration(weapon);
    std::vector<::xr64::N64RawFast3DReplacementBatch> batches;
    std::vector<GLuint> textures;
    std::array<float,4> placement{};
    std::string status="Adjust model to the cyan aim ray. Save with A / Enter.";
    const auto load=[&]() {
        if(!textures.empty())glDeleteTextures(static_cast<GLsizei>(textures.size()),textures.data());
        textures.clear();batches.clear();calibration=load_weapon_calibration(weapon);
        const auto dir=weapon_asset_directory();
        std::ifstream p(dir/(std::to_string(weapon)+".placement"));
        bool valid=bool(p>>placement[0]>>placement[1]>>placement[2]>>placement[3]);
        for(float v:placement)valid=valid&&std::isfinite(v);
        valid=valid&&placement[0]>0&&placement[0]<=0.01F;
        for(int i=1;i<4;++i)valid=valid&&std::abs(placement[i])<=100000;
        if(!valid||!load_preview_weapon(dir/(std::to_string(weapon)+".rwpm"),batches)){
            batches.clear();status="Weapon model unavailable. Select your extracted models folder in the launcher.";return;
        }
        textures.resize(batches.size());glGenTextures(static_cast<GLsizei>(textures.size()),textures.data());
        for(std::size_t i=0;i<batches.size();++i){auto& t=batches[i].texture;glBindTexture(GL_TEXTURE_2D,textures[i]);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,t.width,t.height,0,GL_RGBA,GL_UNSIGNED_BYTE,t.rgba.data());}
        status="Adjust model to the cyan aim ray. Save with A / Enter.";
    };
    load();
    XrMotionInput previous{};std::array<bool,SDL_NUM_SCANCODES> prior_keys{};
    auto last=std::chrono::steady_clock::now(),next_step=last;
    const auto started=last;
    bool running=true,ok=true,seen_session=false,dirty=false;
    while(running&&pump_events()) {
        int n=0;const auto* keys=g_sdl.get_keyboard_state(&n);
        const auto pressed=[&](int code){return window_focused_&&code<n&&keys[code]&&!prior_keys[code];};
        const auto motion=g_xr.input_snapshot();const bool focused=motion.available&&motion.focused;
        if(pressed(SDL_SCANCODE_ESCAPE)||(focused&&motion.right.secondary&&!previous.right.secondary))break;
        const int weapon_delta=(pressed(SDL_SCANCODE_E)||(focused&&motion.left.secondary&&!previous.left.secondary)?1:0)
            -(pressed(SDL_SCANCODE_Q)||(focused&&motion.left.primary&&!previous.left.primary)?1:0);
        if(weapon_delta){weapon=static_cast<unsigned>((int(weapon)-1+weapon_delta+14)%14+1);load();dirty=false;}
        const auto now=std::chrono::steady_clock::now();
        const float dt=std::clamp(std::chrono::duration<float>(now-last).count(),0.0F,0.05F);last=now;
        int field_delta=(pressed(SDL_SCANCODE_DOWN)?1:0)-(pressed(SDL_SCANCODE_UP)?1:0);
        if(focused&&now>=next_step&&std::abs(motion.left.stick[1])>0.65F){field_delta=motion.left.stick[1]>0?-1:1;next_step=now+std::chrono::milliseconds(240);}
        if(field_delta)field=static_cast<unsigned>((int(field)+field_delta+7)%7);
        float change=0;
        if(window_focused_)change=float(keys[SDL_SCANCODE_RIGHT]!=0)-float(keys[SDL_SCANCODE_LEFT]!=0);
        if(focused&&std::abs(motion.right.stick[0])>0.2F)change=motion.right.stick[0];
        if(change&&!batches.empty()) {
            if(field<3)calibration.offset[field]=std::clamp(calibration.offset[field]+change*dt*0.04F,-0.5F,0.5F);
            else if(field<6)calibration.degrees[field-3]=std::clamp(calibration.degrees[field-3]+change*dt*15,-90.0F,90.0F);
            else calibration.scale=std::clamp(calibration.scale+change*dt*0.15F,0.25F,2.0F);
            dirty=true;
        }
        if(pressed(SDL_SCANCODE_R)||(focused&&motion.menu_button&&!previous.menu_button)){calibration={};dirty=true;}
        if(pressed(SDL_SCANCODE_RETURN)||(focused&&motion.right.primary&&!previous.right.primary)) {
            if(batches.empty())status="Cannot calibrate a missing weapon model.";
            else if(save_weapon_calibration(weapon,calibration,error)){status="Calibration saved for this weapon.";dirty=false;}
            else {status=error;error.clear();}
        }
        for(int i=0;i<n&&i<SDL_NUM_SCANCODES;++i)prior_keys[i]=keys[i]!=0;previous=motion;
        bool updated=false;
        ok=g_xr.render([&](const XrEyeFrame& eye){
            const auto current=g_xr.input_snapshot();
            glViewport(0,0,eye.width,eye.height);glDisable(GL_SCISSOR_TEST);
            glClearColor(0.025F,0.035F,0.045F,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
            glDisable(GL_CULL_FACE);glDisable(GL_LIGHTING);glDisable(GL_BLEND);glDisable(GL_TEXTURE_2D);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);
            const auto vertex=[&](const std::array<float,3>& p){auto v=xr_project_tracked_point(p,eye);glVertex4f(v.x,v.y,v.z,v.w);};
            glColor3f(0.13F,0.25F,0.3F);glBegin(GL_LINES);
            for(int i=-5;i<=5;++i){vertex({float(i),-1.5F,-1});vertex({float(i),-1.5F,-10});vertex({-5,-1.5F,float(i)-6});vertex({5,-1.5F,float(i)-6});}glEnd();
            glColor3f(0.7F,0.5F,0.15F);glBegin(GL_LINE_LOOP);vertex({-0.6F,-0.6F,-3});vertex({0.6F,-0.6F,-3});vertex({0.6F,0.6F,-3});vertex({-0.6F,0.6F,-3});glEnd();
            if(current.available&&current.focused&&current.right.pose_valid&&current.right.aim_pose_valid){
                XrShotQuat q;std::copy_n(current.right.aim_orientation,4,q.begin());
                if(xr_normalize_shot_quat(q)) {
                    glEnable(GL_TEXTURE_2D);glColor3f(1,1,1);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
                    for(std::size_t i=0;i<batches.size();++i){glBindTexture(GL_TEXTURE_2D,textures[i]);glBegin(GL_TRIANGLES);
                        for(const auto& v:batches[i].vertices){std::array<float,3> p{(v.x-placement[1])*placement[0],(v.z-placement[3])*placement[0],(v.y-placement[2])*placement[0]};
                            p=rotate_weapon_point(calibrated_weapon_point(p,calibration),q);for(int j=0;j<3;++j)p[j]+=current.right.position[j];
                            glTexCoord2f(v.s,v.t);vertex(p);}glEnd();}
                    glDisable(GL_TEXTURE_2D);
                    XrTrackedRay ray;if(xr_tracked_aim_ray(current.right,ray)){
                        std::array<float,3> end;for(int i=0;i<3;++i)end[i]=ray.origin[i]+ray.forward[i]*5;
                        glDisable(GL_DEPTH_TEST);glColor3f(0,1,1);glBegin(GL_LINES);vertex(ray.origin);vertex(end);glEnd();
                        if(current.right.trigger>0.6F&&ray.forward[2]<-0.01F){const float t=(-3-ray.origin[2])/ray.forward[2];
                            if(t>0&&t<20){std::array<float,3> hit;for(int i=0;i<3;++i)hit[i]=ray.origin[i]+ray.forward[i]*t;
                                glPointSize(10);glColor3f(0.2F,1,0.3F);glBegin(GL_POINTS);vertex(hit);glEnd();}}
                    }
                }
            }
            glDisable(GL_DEPTH_TEST);glDisable(GL_TEXTURE_2D);glColor3f(0.9F,0.95F,1);
            const auto label=[&](float y,const std::string& t){glRasterPos2f(-0.65F,y);glListBase(font-32);glCallLists(static_cast<GLsizei>(t.size()),GL_UNSIGNED_BYTE,t.data());};
            const char* fields[]={"Position X","Position Y","Position Z","Pitch","Yaw","Roll","Scale"};
            const float value=field<3?calibration.offset[field]*100:field<6?calibration.degrees[field-3]:calibration.scale;
            char line[160];std::snprintf(line,sizeof(line),"WEAPON %u | %s: %.2f %s %s",weapon,fields[field],value,field<3?"cm":field<6?"deg":"x",dirty?"(unsaved)":"");
            label(0.70F,"WEAPON CALIBRATION - SAFE PREVIEW");label(0.60F,line);
            label(0.50F,"L stick: setting | R stick: adjust | X/Y: weapon");
            label(0.40F,"A: save | B: exit | Menu: reset | Trigger: aim test");
            label(-0.60F,status);label(-0.70F,"Unsaved edits discarded on weapon change / exit.");
            updated=true;return true;
        },[&](){g_sdl.gl_swap_window(window);return true;},error,true);
        if(!ok)break;
        if(g_xr.session_running())seen_session=true;
        if((seen_session&&!g_xr.session_running())||(!seen_session&&now-started>std::chrono::seconds(15))){error="VR calibration session ended or was not ready.";ok=false;break;}
        if(!updated)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if(!textures.empty())glDeleteTextures(static_cast<GLsizei>(textures.size()),textures.data());
    glDeleteLists(font,96);shutdown();return ok;
#else
    error="VR calibration requires an OpenXR build.";return false;
#endif
}
bool run_gate5_weapon_calibration(std::string& error) {
    RageWarsDesktopRenderer renderer;return renderer.run_weapon_calibration(error);
}
