#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <rex/audio/title_playlist_player.h>
#include <rex/memory/mapped_memory.h>

int main(int argc, char** argv) {
  const std::filesystem::path song =
      argc > 1 ? std::filesystem::path(argv[1])
               : std::filesystem::path("game/dat/sound/songs/BahaMen1.mp3");
  auto mapping = rex::memory::MappedMemory::Open(song, rex::memory::MappedMemory::Mode::kRead);
  if (!mapping) {
    std::cerr << "FAIL: could not map " << song.string() << '\n';
    return 1;
  }
  std::vector<rex::audio::TitlePlaylistTrack> tracks;
  tracks.push_back({std::shared_ptr<rex::memory::MappedMemory>(mapping.release()), "decode test"});
  if (!rex::audio::PlayTitlePlaylist(std::move(tracks), 0)) {
    std::cerr << "FAIL: playlist was rejected\n";
    return 1;
  }

  rex::audio::TitlePlaylistDiagnostics diagnostics;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  do {
    diagnostics = rex::audio::GetTitlePlaylistDiagnostics();
    if (diagnostics.decode_failed) {
      std::cerr << "FAIL: decoder reported failure\n";
      return 1;
    }
    if (diagnostics.decoded_stereo_frames) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while (std::chrono::steady_clock::now() < deadline);
  if (!diagnostics.decoded_stereo_frames) {
    std::cerr << "FAIL: decoder timed out\n";
    return 1;
  }

  std::vector<float> output(48000 * 2, 0.0f);
  rex::audio::MixTitlePlaylist(output.data(), 48000, 2);
  const size_t nonzero = static_cast<size_t>(
      std::count_if(output.begin(), output.end(), [](float sample) { return std::abs(sample) > 1e-6f; }));
  const float peak = *std::max_element(output.begin(), output.end(), [](float a, float b) {
    return std::abs(a) < std::abs(b);
  });
  diagnostics = rex::audio::GetTitlePlaylistDiagnostics();
  if (!nonzero || diagnostics.mixed_stereo_frames != 48000 ||
      std::abs(diagnostics.requested_volume - 1.0f) > 1e-6f ||
      std::abs(diagnostics.effective_gain - 0.25f) > 1e-6f) {
    rex::audio::StopTitlePlaylist();
    std::cerr << "FAIL: decoded music did not reach the output mixer\n";
    return 1;
  }

  rex::audio::PauseTitlePlaylist();
  std::vector<float> control_output(256 * 2, 0.0f);
  rex::audio::MixTitlePlaylist(control_output.data(), 256, 2);
  auto paused_diagnostics = rex::audio::GetTitlePlaylistDiagnostics();
  if (!paused_diagnostics.paused || paused_diagnostics.mixed_stereo_frames != 48000 ||
      std::any_of(control_output.begin(), control_output.end(),
                  [](float sample) { return sample != 0.0f; })) {
    rex::audio::StopTitlePlaylist();
    std::cerr << "FAIL: pause did not silence and freeze title music\n";
    return 1;
  }

  rex::audio::SetTitlePlaylistVolume(0.0f);
  rex::audio::ContinueTitlePlaylist();
  rex::audio::MixTitlePlaylist(control_output.data(), 256, 2);
  auto muted_diagnostics = rex::audio::GetTitlePlaylistDiagnostics();
  if (muted_diagnostics.paused || muted_diagnostics.mixed_stereo_frames != 48256 ||
      muted_diagnostics.requested_volume != 0.0f || muted_diagnostics.effective_gain != 0.0f ||
      std::any_of(control_output.begin(), control_output.end(),
                  [](float sample) { return sample != 0.0f; })) {
    rex::audio::StopTitlePlaylist();
    std::cerr << "FAIL: zero-volume playback did not advance silently\n";
    return 1;
  }
  rex::audio::SetTitlePlaylistVolume(0.5f);
  auto half_volume_diagnostics = rex::audio::GetTitlePlaylistDiagnostics();
  if (std::abs(half_volume_diagnostics.requested_volume - 0.5f) > 1e-6f ||
      std::abs(half_volume_diagnostics.effective_gain - 0.125f) > 1e-6f) {
    rex::audio::StopTitlePlaylist();
    std::cerr << "FAIL: title music volume did not apply the base gain and slider gain\n";
    return 1;
  }
  rex::audio::SetTitlePlaylistVolume(1.0f);
  rex::audio::StopTitlePlaylist();
  std::cout << "PASS: MP3 decoded to " << diagnostics.decoded_stereo_frames
            << " stereo frames at 48 kHz; mixed 48000 frames, nonzero samples=" << nonzero
            << ", peak=" << std::abs(peak)
            << "; pause/continue/mute/base-gain/slider-gain controls passed\n";
  return 0;
}
