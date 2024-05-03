#include "adf_media_player.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF

namespace esphome {
namespace esp_adf {

static const char *const TAG = "adf_media_player";

void ADFMediaPlayer::setup() {
  state = media_player::MEDIA_PLAYER_STATE_IDLE;
}

void ADFMediaPlayer::dump_config() {
  esph_log_config(TAG, "ESP-ADF-MediaPlayer:");
  int components = pipeline.get_number_of_elements();
  esph_log_config(TAG, "  Number of ASPComponents: %d", components);
}

void ADFMediaPlayer::set_stream_uri(const std::string& new_uri) {
  this->current_uri_ = new_uri;
  http_and_decoder_.set_stream_uri(new_uri);
}

void ADFMediaPlayer::set_announcement_uri(const std::string& new_uri) {
  this->announcement_uri_ = new_uri;
  http_and_decoder_.set_stream_uri(new_uri);
}


void ADFMediaPlayer::control(const media_player::MediaPlayerCall &call) {
  if (call.get_media_url().has_value()) {
    if (call.get_announcement().has_value()){
      this->announcement_ =  call.get_announcement().value();
    }
    if( this->announcement_){
      set_announcement_uri( call.get_media_url().value()) ;
      //this->http_and_decoder_.set_fixed_settings(true);
    }
    else{
      set_stream_uri( call.get_media_url().value()) ;
    }

    esph_log_d(TAG, "Got control call in state %s", media_player_state_to_string(this->state) );
    switch(this->state){
      case media_player::MEDIA_PLAYER_STATE_IDLE:
      case media_player::MEDIA_PLAYER_STATE_PAUSED:
        this->http_and_decoder_.set_fixed_settings(this->announcement_);
        pipeline.start();
        return;
      case media_player::MEDIA_PLAYER_STATE_PLAYING:
        this->play_intent_ = true;
        pipeline.stop();
        return;
      case media_player::MEDIA_PLAYER_STATE_ANNOUNCING:
        this->play_intent_ = true;
        return;
      default:
        break;
    }
  }

  if (call.get_volume().has_value()) {
    set_volume_(call.get_volume().value());
  }

  if (call.get_command().has_value()) {
    switch (call.get_command().value()) {
      case media_player::MEDIA_PLAYER_COMMAND_PLAY:
        if (pipeline.getState() == PipelineState::STOPPED || pipeline.getState() == PipelineState::UNINITIALIZED) {
          pipeline.start();
        } else if (pipeline.getState() == PipelineState::PAUSED) {
          pipeline.restart();
        }
        else if (state == media_player::MEDIA_PLAYER_STATE_PLAYING || state == media_player::MEDIA_PLAYER_STATE_PAUSED){
          this->play_intent_ = true;
          pipeline.stop();
        }
        break;
      case media_player::MEDIA_PLAYER_COMMAND_PAUSE:
        if (pipeline.getState() == PipelineState::RUNNING) {
          pipeline.pause();
        }
        break;
      case media_player::MEDIA_PLAYER_COMMAND_STOP:
        this->current_uri_.reset();
        pipeline.stop();
        this->http_and_decoder_.set_fixed_settings(false);
        break;
      case media_player::MEDIA_PLAYER_COMMAND_MUTE:
        this->mute_();
        break;
      case media_player::MEDIA_PLAYER_COMMAND_UNMUTE:
        this->unmute_();
        break;
      case media_player::MEDIA_PLAYER_COMMAND_TOGGLE:
        if (pipeline.getState() == PipelineState::STOPPED || pipeline.getState() == PipelineState::PAUSED) {
          state = media_player::MEDIA_PLAYER_STATE_PLAYING;
        } else {
          state = media_player::MEDIA_PLAYER_STATE_PAUSED;
        }
        break;
      case media_player::MEDIA_PLAYER_COMMAND_VOLUME_UP: {
        float new_volume = this->volume + 0.1f;
        if (new_volume > 1.0f)
          new_volume = 1.0f;
        set_volume_(new_volume);
        break;
      }
      case media_player::MEDIA_PLAYER_COMMAND_VOLUME_DOWN: {
        float new_volume = this->volume - 0.1f;
        if (new_volume < 0.0f)
          new_volume = 0.0f;
        set_volume_(new_volume);
        break;
      }
    }
  }
}

// pausing is only supported if destroy_pipeline_on_stop is disabled
media_player::MediaPlayerTraits ADFMediaPlayer::get_traits() {
  auto traits = media_player::MediaPlayerTraits();
  traits.set_supports_pause( false );
  return traits;
};

void ADFMediaPlayer::mute_() {
  AudioPipelineSettingsRequest request;
  request.mute = 1;
  if (pipeline.request_settings(request)) {
    muted_ = true;
    publish_state();
  }
}

void ADFMediaPlayer::unmute_() {
  AudioPipelineSettingsRequest request;
  request.mute = 0;
  if (pipeline.request_settings(request)) {
    muted_ = false;
    publish_state();
  }
}

void ADFMediaPlayer::set_volume_(float volume, bool publish) {
  AudioPipelineSettingsRequest request;
  request.target_volume = volume;
  if( volume > 0 ){
    request.mute = 0;
  }
  if (pipeline.request_settings(request)) {
    this->volume = volume;
    if (publish && false)
      publish_state();
  }
}

void ADFMediaPlayer::on_pipeline_state_change(PipelineState state) {
  esph_log_i(TAG, "got new pipeline state: %d, while in MP state %s", (int) state, media_player_state_to_string(this->state));
  switch (state) {
    case PipelineState::UNINITIALIZED:
    case PipelineState::STOPPED:
    case PipelineState::PAUSED:
      if( this->state == media_player::MEDIA_PLAYER_STATE_ANNOUNCING )
      {
        this->state = media_player::MEDIA_PLAYER_STATE_IDLE;
        this->announcement_ = false;
        if( this->current_uri_.has_value() )
        {
          this->http_and_decoder_.set_stream_uri(this->current_uri_.value());
          this->http_and_decoder_.set_fixed_settings(false);
          pipeline.restart();
        }
        this->play_intent_ = false;
      } else if (this->play_intent_) {
        set_new_state(media_player::MEDIA_PLAYER_STATE_IDLE);
        this->http_and_decoder_.set_fixed_settings(this->announcement_);
        pipeline.restart();
        this->play_intent_ = false;
      } else if (state == PipelineState::PAUSED)
      {
        if( set_new_state(media_player::MEDIA_PLAYER_STATE_PAUSED))
        {
          publish_state();
        }
      } else if (set_new_state(media_player::MEDIA_PLAYER_STATE_IDLE) ){
        publish_state();
      }
      break;

    case PipelineState::PREPARING:
    case PipelineState::STARTING:
    case PipelineState::RUNNING:
      {
        media_player::MediaPlayerState target = (
          this->announcement_ ? media_player::MEDIA_PLAYER_STATE_ANNOUNCING
          : media_player::MEDIA_PLAYER_STATE_PLAYING );
        if( set_new_state(target) ){
          this->set_volume_( this->volume, false);
          publish_state();
        }
      }
      break;
    default:
      break;
  }
  esph_log_i(TAG, "current mp state: %s", media_player_state_to_string(this->state));
  esph_log_i(TAG, "anouncement: %s", this->announcement_ ? "yes" : "false");
  esph_log_i(TAG, "play_intent: %s", this->play_intent_ ? "yes" : "false");
  esph_log_i(TAG, "current_uri_: %s", this->current_uri_.has_value() ? "yes" : "false");

}

void ADFMediaPlayer::loop(){
  pipeline.loop();
}



}  // namespace esp_adf
}  // namespace esphome
#endif
