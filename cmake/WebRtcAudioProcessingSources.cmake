# Source closure for the two WebRTC facilities Cupuacu uses directly:
# EchoCanceller3 and PushSincResampler. Keep paths relative to webrtc/.
#
# This list deliberately follows the portable implementation. Architecture-
# specific sources are listed separately below because the portable x86 code
# contains runtime-dispatch references to its SSE/AVX implementations.
set(CUPUACU_WEBRTC_AUDIO_PROCESSING_SOURCES
    api/audio/echo_canceller3_config.cc

    common_audio/audio_util.cc
    common_audio/resampler/push_sinc_resampler.cc
    common_audio/resampler/sinc_resampler.cc
    common_audio/signal_processing/splitting_filter.c
    common_audio/third_party/ooura/fft_size_128/ooura_fft.cc

    modules/audio_processing/aec3/adaptive_fir_filter.cc
    modules/audio_processing/aec3/adaptive_fir_filter_erl.cc
    modules/audio_processing/aec3/aec3_common.cc
    modules/audio_processing/aec3/aec3_fft.cc
    modules/audio_processing/aec3/aec_state.cc
    modules/audio_processing/aec3/alignment_mixer.cc
    modules/audio_processing/aec3/api_call_jitter_metrics.cc
    modules/audio_processing/aec3/block_buffer.cc
    modules/audio_processing/aec3/block_delay_buffer.cc
    modules/audio_processing/aec3/block_framer.cc
    modules/audio_processing/aec3/block_processor.cc
    modules/audio_processing/aec3/block_processor_metrics.cc
    modules/audio_processing/aec3/clockdrift_detector.cc
    modules/audio_processing/aec3/coarse_filter_update_gain.cc
    modules/audio_processing/aec3/comfort_noise_generator.cc
    modules/audio_processing/aec3/config_selector.cc
    modules/audio_processing/aec3/decimator.cc
    modules/audio_processing/aec3/dominant_nearend_detector.cc
    modules/audio_processing/aec3/downsampled_render_buffer.cc
    modules/audio_processing/aec3/echo_audibility.cc
    modules/audio_processing/aec3/echo_canceller3.cc
    modules/audio_processing/aec3/echo_path_delay_estimator.cc
    modules/audio_processing/aec3/echo_path_variability.cc
    modules/audio_processing/aec3/echo_remover.cc
    modules/audio_processing/aec3/echo_remover_metrics.cc
    modules/audio_processing/aec3/erl_estimator.cc
    modules/audio_processing/aec3/erle_estimator.cc
    modules/audio_processing/aec3/fft_buffer.cc
    modules/audio_processing/aec3/filter_analyzer.cc
    modules/audio_processing/aec3/frame_blocker.cc
    modules/audio_processing/aec3/fullband_erle_estimator.cc
    modules/audio_processing/aec3/matched_filter.cc
    modules/audio_processing/aec3/matched_filter_lag_aggregator.cc
    modules/audio_processing/aec3/moving_average.cc
    modules/audio_processing/aec3/multi_channel_content_detector.cc
    modules/audio_processing/aec3/refined_filter_update_gain.cc
    modules/audio_processing/aec3/render_buffer.cc
    modules/audio_processing/aec3/render_delay_buffer.cc
    modules/audio_processing/aec3/render_delay_controller.cc
    modules/audio_processing/aec3/render_delay_controller_metrics.cc
    modules/audio_processing/aec3/render_signal_analyzer.cc
    modules/audio_processing/aec3/residual_echo_estimator.cc
    modules/audio_processing/aec3/reverb_decay_estimator.cc
    modules/audio_processing/aec3/reverb_frequency_response.cc
    modules/audio_processing/aec3/reverb_model.cc
    modules/audio_processing/aec3/reverb_model_estimator.cc
    modules/audio_processing/aec3/signal_dependent_erle_estimator.cc
    modules/audio_processing/aec3/spectrum_buffer.cc
    modules/audio_processing/aec3/stationarity_estimator.cc
    modules/audio_processing/aec3/subband_erle_estimator.cc
    modules/audio_processing/aec3/subband_nearend_detector.cc
    modules/audio_processing/aec3/subtractor.cc
    modules/audio_processing/aec3/subtractor_output.cc
    modules/audio_processing/aec3/subtractor_output_analyzer.cc
    modules/audio_processing/aec3/suppression_filter.cc
    modules/audio_processing/aec3/suppression_gain.cc
    modules/audio_processing/aec3/transparent_mode.cc

    modules/audio_processing/audio_buffer.cc
    modules/audio_processing/high_pass_filter.cc
    modules/audio_processing/logging/apm_data_dumper.cc
    modules/audio_processing/splitting_filter.cc
    modules/audio_processing/three_band_filter_bank.cc
    modules/audio_processing/utility/cascaded_biquad_filter.cc

    rtc_base/checks.cc
    rtc_base/experiments/field_trial_parser.cc
    rtc_base/logging.cc
    rtc_base/memory/aligned_malloc.cc
    rtc_base/platform_thread_types.cc
    rtc_base/race_checker.cc
    rtc_base/string_encode.cc
    rtc_base/string_utils.cc
    rtc_base/strings/string_builder.cc
    rtc_base/system_time.cc
    rtc_base/time_utils.cc

    system_wrappers/source/field_trial.cc
    system_wrappers/source/metrics.cc
)

# Minimal link closure for an x86 or x86_64 slice. These implementations are
# runtime-dispatch targets referenced by the portable sources above.
set(CUPUACU_WEBRTC_AUDIO_PROCESSING_X86_SOURCES
    common_audio/resampler/sinc_resampler_avx2.cc
    common_audio/resampler/sinc_resampler_sse.cc
    common_audio/third_party/ooura/fft_size_128/ooura_fft_sse2.cc
    modules/audio_processing/aec3/adaptive_fir_filter_avx2.cc
    modules/audio_processing/aec3/adaptive_fir_filter_erl_avx2.cc
    modules/audio_processing/aec3/fft_data_avx2.cc
    modules/audio_processing/aec3/matched_filter_avx2.cc
    system_wrappers/source/cpu_features.cc
)
