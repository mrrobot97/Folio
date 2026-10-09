/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实电源编排与锁屏实现的故障注入，PMU 指令仅计数不接触设备。
 * English: Fault injection against real power/lock code; PMU calls only count, never touch a device.
 */
#include "folio_power.h"
#include "folio_ui.h"
#include "reader_runtime.h"
#include "ui_kit.h"
#include "sleep.h"
#include "read_pico_pmu.h"
#include "driver/gpio.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if(!(test)){fprintf(stderr,"FAIL %s:%d: %s\n",__func__,__LINE__,#test);failures++;} } while(0)
#define MAX_EVENTS 64
static int failures,checks;
static const char *case_name;
static uint8_t framebuffer[16];
static jmp_buf power_cut;
static bool jump_armed;
static app_sleep_mode_t selected_mode;
static int64_t now_us,prepare_delay_us;
static int prepare_calls,wake_calls,network_calls,lock_frames,light_sleeps,panel_poweroffs,pmu_off,pmu_eco,selftest_calls,delays;
static bool network_ok,command_failure,bad_wallpaper;
static int save_plan[16],save_plan_count;
static bool network_plan[8],prepare_eco[16];
static int network_plan_count;
static bool has_draft,editor_plan[4];
static int editor_plan_count,editor_calls;
static const app_desc_t editor_destination={.title="排版编辑"};
static app_ctx_t request_ctx;
static folio_reader_save_status_t save_status;
static bool observed_rows[3];
static int row_mask;
static char drawn_text[16384];
static struct {cst836u_touch_t touch;bool power_key,error;} events[MAX_EVENTS];
static size_t event_count,event_index;
static bool unexpected_modal;
const uint8_t lock_asset[] asm("_binary_lock_4bpp_bin_start")={0};

static void reset(const char *name){
    case_name=name;selected_mode=APP_SLEEP_DEEP;now_us=1000;prepare_delay_us=10000;
    prepare_calls=wake_calls=network_calls=lock_frames=light_sleeps=panel_poweroffs=pmu_off=pmu_eco=selftest_calls=delays=0;
    network_ok=true;command_failure=bad_wallpaper=false;has_draft=false;editor_plan_count=editor_calls=0;save_plan_count=network_plan_count=0;memset(prepare_eco,0,sizeof(prepare_eco));save_status=(folio_reader_save_status_t){false,false,false};
    memset(observed_rows,0,sizeof(observed_rows));row_mask=0;drawn_text[0]=0;event_count=event_index=0;unexpected_modal=false;
}
static void add_touch(bool touched,int x,int y,int count){
    if(event_count>=MAX_EVENTS)abort();
    events[event_count++]=(typeof(events[0])){.touch={.touched=touched,.count=(uint8_t)count,.x=(uint16_t)x,.y=(uint16_t)y}};
}
static void add_key(void){events[event_count++]=(typeof(events[0])){.power_key=true};}
static void add_cancel(void){add_touch(false,0,0,0);add_touch(true,180,1010,1);add_touch(false,180,1010,0);}
static void add_retry(void){add_touch(false,0,0,0);add_touch(true,500,1010,1);add_touch(false,500,1010,0);}
static void record(const char *text){size_t n=strlen(drawn_text),k=strlen(text);if(n+k+2<sizeof(drawn_text)){memcpy(drawn_text+n,text,k);drawn_text[n+k]='\n';drawn_text[n+k+1]=0;}}
static void run_request(bool poweroff){
    EpdiyHighlevelState hl={0};request_ctx=(app_ctx_t){.hl=&hl,.fb=framebuffer,.tp=(void*)1};int64_t ignored=0;
    jump_armed=true;
    if(setjmp(power_cut)==0)folio_power_request(&request_ctx,poweroff,&ignored);
    jump_armed=false;
}
static void finish(void){checks++;printf("%s: saves=%d lock=%d fast=%d PMU-eco=%d PMU-off=%d wake=%d\n",case_name,prepare_calls,lock_frames,light_sleeps,pmu_eco,pmu_off,wake_calls);}

bool folio_reader_prepare_sleep(bool eco){
    int mask=prepare_calls<save_plan_count?save_plan[prepare_calls]:7;if(prepare_calls<16)prepare_eco[prepare_calls]=eco;prepare_calls++;now_us+=prepare_delay_us;
    save_status=(folio_reader_save_status_t){.progress_ok=(mask&1)!=0,.statistics_ok=(mask&2)!=0,.draft_ok=(mask&4)!=0};return mask==7;
}
folio_reader_save_status_t folio_reader_save_status(void){return save_status;}
void folio_reader_wake(void){wake_calls++;}
bool folio_reader_has_unapplied_draft(void){return has_draft;}
bool folio_reader_return_to_editor(app_ctx_t*ctx){
    bool ok=editor_calls<editor_plan_count?editor_plan[editor_calls]:true;editor_calls++;
    if(ok)ctx->request_app=&editor_destination;
    return ok;
}
const char*folio_reader_blocked_reason(void){return "原图书缺失或已更改，排版草稿已保留";}
bool read_pico_transfer_try_stop_if_idle(void){bool stopped=network_calls<network_plan_count?network_plan[network_calls]:network_ok;network_calls++;return stopped;}
app_sleep_mode_t app_settings_sleep_mode(void){return selected_mode;}
const char*app_sleep_mode_name(app_sleep_mode_t mode){(void)mode;return "test";}
bool app_settings_pickup_wake(void){return false;}
const char*app_settings_wallpaper_path(void){return bad_wallpaper?"/sdcard/wallpapers/missing.fwp":"";}
uint8_t app_settings_last_wake(void){return 0;}
void app_settings_set_last_wake(uint8_t src){(void)src;}
bool folio_wallpaper_read(const char*path,uint8_t**out){(void)path;(void)out;return false;}
int64_t esp_timer_get_time(void){return now_us;}
void vTaskDelay(int ticks){now_us+=(int64_t)ticks*1000;if(++delays>1000){fprintf(stderr,"FAIL infinite wait: %s\n",case_name);abort();}}
const pmu_snapshot_t*read_pico_pmu_get(void){static pmu_snapshot_t p={0};return &p;}
void read_pico_pmu_drain_events(void){}
esp_err_t read_pico_pmu_poll(void){return ESP_OK;}
bool read_pico_pmu_take_key_short(void){
    if(event_index>=event_count){unexpected_modal=true;return true;}
    if(events[event_index].power_key){event_index++;return true;}return false;
}
bool read_pico_pmu_take_key_wakeup(void){return light_sleeps>0;}
esp_err_t cst836u_read(cst836u_handle_t h,cst836u_touch_t*out){
    (void)h;if(event_index>=event_count)abort();*out=events[event_index].touch;bool error=events[event_index].error;event_index++;return error?ESP_FAIL:ESP_OK;
}
static esp_err_t pmu_command(bool off){if(off)pmu_off++;else pmu_eco++;if(command_failure)return ESP_FAIL;if(!jump_armed)abort();longjmp(power_cut,1);}
esp_err_t read_pico_pmu_power_off(void){return pmu_command(true);}
esp_err_t read_pico_pmu_report_sleep(void){return pmu_command(false);}
void pmu_selftest_prepare_powerdown(void){selftest_calls++;}
void read_pico_clear_ioe_int(void){}
void gpio_config(const gpio_config_t*c){(void)c;}
void gpio_wakeup_enable(gpio_num_t g,int type){(void)g;(void)type;}
void gpio_wakeup_disable(gpio_num_t g){(void)g;}
int gpio_get_level(gpio_num_t g){(void)g;return 1;}
void esp_sleep_enable_gpio_wakeup(void){}
void esp_light_sleep_start(void){light_sleeps++;now_us+=500000;}
void epd_poweroff(void){panel_poweroffs++;}
uint8_t*epd_hl_get_framebuffer(EpdiyHighlevelState*h){(void)h;return framebuffer;}
void epd_fill_rect(EpdRect r,uint8_t color,uint8_t*fb){(void)r;(void)color;(void)fb;}
enum EpdDrawError update_display_mode(EpdiyHighlevelState*h,enum EpdDrawMode mode){(void)h;CHECK(mode==MODE_GL16);return EPD_DRAW_SUCCESS;}
void guard_draw_result(EpdiyHighlevelState*h,enum EpdDrawError err){(void)h;CHECK(err==EPD_DRAW_SUCCESS);}
void ui_draw_full_image(uint8_t*fb,const uint8_t*image){(void)fb;(void)image;lock_frames++;}
void ui_text(uint8_t*fb,int x,int y,int px,const char*text,enum EpdFontFlags align,bool inv){(void)fb;(void)x;(void)y;(void)px;(void)align;(void)inv;record(text);}
bool ui_rect_hit(EpdRect r,uint16_t x,uint16_t y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
void folio_ui_sample_status(folio_status_t*out){memset(out,0,sizeof(*out));}
void folio_ui_header(uint8_t*fb,const folio_status_t*status,const char*title,bool back){(void)fb;(void)status;(void)back;record(title);}
void folio_ui_text(uint8_t*fb,int x,int y,int px,const char*text,enum EpdFontFlags align,uint8_t ink,uint8_t paper){(void)fb;(void)x;(void)y;(void)px;(void)align;(void)ink;(void)paper;record(text);}
void folio_ui_text_fit(uint8_t*fb,int x,int y,int px,int width,const char*text,uint8_t ink,uint8_t paper){(void)fb;(void)x;(void)y;(void)px;(void)width;(void)ink;(void)paper;record(text);}
void folio_ui_row(uint8_t*fb,int y,const char*label,const char*value,uint8_t paper){
    (void)fb;(void)y;(void)paper;int i=!strcmp(label,"阅读进度")?0:!strcmp(label,"阅读统计")?1:2;observed_rows[i]=true;if(!strcmp(value,"已保存"))row_mask|=1<<i;else row_mask&=~(1<<i);record(label);record(value);
}
void folio_ui_button(uint8_t*fb,EpdRect r,const char*label,bool primary){(void)fb;(void)r;(void)primary;record(label);}
esp_err_t sc7a20h_read_events(sc7a20h_handle_t a,sc7a20h_events_t*e){(void)a;e->aoi1_src=0;return ESP_OK;}
void sc7a20h_ack_int(sc7a20h_handle_t a){(void)a;}
void sc7a20h_aoi_decode(uint8_t raw,sc7a20h_aoi_src_t*out){(void)raw;out->ia=false;}
int sc7a20h_int1_level(sc7a20h_handle_t a){(void)a;return 0;}
int sc7a20h_int1_gpio(sc7a20h_handle_t a){(void)a;return 1;}
void sc7a20h_arm_pickup_wake(sc7a20h_handle_t a,const sc7a20h_motion_cfg_t*c){(void)a;(void)c;}
void sc7a20h_config_light_sleep_wakeup(sc7a20h_handle_t a){(void)a;}
void sc7a20h_power_down(sc7a20h_handle_t a){(void)a;}
void sc7a20h_int1_begin(sc7a20h_handle_t a){(void)a;}

static void test_eco_failure(void){reset("ECO save failure");save_plan[0]=save_plan[1]=0;save_plan_count=2;add_cancel();run_request(false);CHECK(prepare_calls==2);CHECK(!lock_frames&&!pmu_off&&!pmu_eco&&!panel_poweroffs);CHECK(wake_calls==1);CHECK(!unexpected_modal);finish();}
static void test_timeout(void){reset("ECO save deadline");prepare_delay_us=2100000;save_plan[0]=0;save_plan_count=1;add_cancel();run_request(false);CHECK(prepare_calls==1);CHECK(!lock_frames&&!pmu_off&&!pmu_eco);finish();}
static void test_manual_retry(void){reset("Manual retry then cancel");for(int i=0;i<4;i++)save_plan[i]=0;save_plan_count=4;add_retry();add_cancel();run_request(false);CHECK(prepare_calls==4);CHECK(!lock_frames&&!pmu_off&&!pmu_eco);CHECK(event_index==event_count);finish();}
static void test_fast_failure(void){reset("FAST unsaved RAM");selected_mode=APP_SLEEP_LIGHT;save_plan[0]=save_plan[1]=3;save_plan_count=2;run_request(false);CHECK(prepare_calls==2);CHECK(!prepare_eco[0]&&!prepare_eco[1]);CHECK(light_sleeps==1&&lock_frames==1);CHECK(!pmu_off&&!pmu_eco);CHECK(!save_status.draft_ok);CHECK(strstr(drawn_text,"仍有记录未保存")!=NULL);CHECK(wake_calls==1);finish();}
static void test_busy(void){reset("Transfer busy");network_ok=false;add_cancel();run_request(true);CHECK(!prepare_calls&&!lock_frames&&!pmu_off&&!pmu_eco);CHECK(strstr(drawn_text,"文件传输尚未结束")!=NULL);finish();}
static void test_component_status(void){for(int mask=0;mask<7;mask++){reset("Independent save ACKs");save_plan[0]=save_plan[1]=mask;save_plan_count=2;add_cancel();run_request(false);CHECK(row_mask==mask);CHECK(observed_rows[0]&&observed_rows[1]&&observed_rows[2]);CHECK(!pmu_off&&!pmu_eco&&!lock_frames);finish();}}
static void test_release_required(void){reset("Inherited held touch ignored");save_plan[0]=save_plan[1]=0;save_plan_count=2;add_touch(true,500,1010,1);add_touch(false,500,1010,0);add_cancel();run_request(false);CHECK(prepare_calls==2);CHECK(!pmu_off&&!pmu_eco);CHECK(event_index==event_count);finish();}
static void test_move_outside(void){reset("Moving outside cancels retry");save_plan[0]=save_plan[1]=0;save_plan_count=2;add_touch(false,0,0,0);add_touch(true,500,1010,1);add_touch(true,500,800,1);add_touch(false,500,800,0);add_key();run_request(false);CHECK(prepare_calls==2);CHECK(!lock_frames&&!pmu_eco&&!pmu_off);finish();}
static void test_multitouch(void){reset("Multitouch cancels retry");save_plan[0]=save_plan[1]=0;save_plan_count=2;add_touch(false,0,0,0);add_touch(true,500,1010,1);add_touch(true,500,1010,2);add_touch(false,500,1010,0);add_key();run_request(false);CHECK(prepare_calls==2);CHECK(!pmu_off&&!pmu_eco);finish();}
static void test_read_error(void){reset("Read error cancels retry");save_plan[0]=save_plan[1]=0;save_plan_count=2;add_touch(false,0,0,0);add_touch(true,500,1010,1);add_touch(true,500,1010,1);events[event_count-1].error=true;add_touch(false,500,1010,0);add_key();run_request(false);CHECK(prepare_calls==2);CHECK(!pmu_off&&!pmu_eco);finish();}
static void test_wallpaper_failure(void){reset("Wallpaper cannot hide unsaved state");selected_mode=APP_SLEEP_LIGHT;bad_wallpaper=true;save_plan[0]=save_plan[1]=3;save_plan_count=2;run_request(false);CHECK(strstr(drawn_text,"未保存")!=NULL);CHECK(!pmu_off&&!pmu_eco);finish();}
static void test_success_bound(void){reset("Successful ECO maximum two save attempts");save_plan[0]=0;save_plan[1]=7;save_plan_count=2;run_request(false);CHECK(prepare_calls<=2);CHECK(pmu_eco==1&&pmu_off==0);finish();}
static void test_leave_then_return(void){reset("Leaving button invalidates whole contact");save_plan[0]=save_plan[1]=0;save_plan_count=2;add_touch(false,0,0,0);add_touch(true,500,1010,1);add_touch(true,500,800,1);add_touch(true,500,1010,1);add_touch(false,500,1010,0);add_key();run_request(false);CHECK(prepare_calls==2);CHECK(!lock_frames&&!pmu_eco&&!pmu_off);finish();}
static void test_latest_status(void){reset("Retry shows latest independent ACKs");save_plan[0]=save_plan[1]=1;save_plan[2]=save_plan[3]=6;save_plan_count=4;add_retry();add_cancel();run_request(false);CHECK(row_mask==6);CHECK(!pmu_eco&&!pmu_off);finish();}
static void test_explicit_poweroff(void){reset("Explicit off overrides FAST");selected_mode=APP_SLEEP_LIGHT;run_request(true);CHECK(pmu_off==1&&pmu_eco==0);CHECK(prepare_eco[0]);CHECK(!light_sleeps);finish();}
static void test_transfer_race(void){reset("Transfer starts before final command");network_plan[0]=true;network_plan[1]=false;network_plan_count=2;add_cancel();run_request(false);CHECK(!pmu_off&&!pmu_eco&&!selftest_calls);finish();}
static void test_pmu_reject(void){reset("PMU failure returns without false success");command_failure=true;add_cancel();run_request(true);CHECK(pmu_off==1&&pmu_eco==0);CHECK(strstr(drawn_text,"电源管理器未确认")!=NULL);CHECK(wake_calls>=1);finish();}
static void test_return_to_editor(void){reset("Dedicated return to RAM editor");has_draft=true;save_plan[0]=save_plan[1]=0;save_plan_count=2;add_cancel();run_request(false);CHECK(editor_calls==1);CHECK(request_ctx.request_app==&editor_destination);CHECK(!pmu_off&&!pmu_eco);finish();}
static void test_missing_resource_cancel(void){reset("Missing resource retains failure page until cancel");has_draft=true;editor_plan[0]=false;editor_plan_count=1;save_plan[0]=save_plan[1]=0;save_plan_count=2;add_cancel();add_key();run_request(false);CHECK(editor_calls==1);CHECK(request_ctx.request_app==NULL);CHECK(strstr(drawn_text,"原图书缺失或已更改")!=NULL);CHECK(prepare_calls==2);CHECK(event_index==event_count);CHECK(!pmu_off&&!pmu_eco);finish();}
static void test_missing_resource_retry(void){reset("Missing resource supports explicit retry and editor return");has_draft=true;editor_plan[0]=false;editor_plan[1]=true;editor_plan_count=2;for(int i=0;i<4;i++)save_plan[i]=0;save_plan_count=4;add_cancel();add_retry();add_cancel();run_request(false);CHECK(editor_calls==2);CHECK(request_ctx.request_app==&editor_destination);CHECK(prepare_calls==4);CHECK(event_index==event_count);CHECK(!pmu_off&&!pmu_eco);finish();}
int main(void){test_eco_failure();test_timeout();test_manual_retry();test_fast_failure();test_busy();test_component_status();test_release_required();test_move_outside();test_multitouch();test_read_error();test_wallpaper_failure();test_success_bound();test_leave_then_return();test_latest_status();test_explicit_poweroff();test_transfer_race();test_pmu_reject();test_return_to_editor();test_missing_resource_cancel();test_missing_resource_retry();printf("Power cases: %d, failures: %d\n",checks,failures);return failures?1:0;}
