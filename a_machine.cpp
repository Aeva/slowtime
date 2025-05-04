#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

#include <print>
#include <vector>
#include <chrono>

#define FRAME_TIME_MODE 1
#define STEADY_TIME_MODE 2

#define EXPERIMENT FRAME_TIME_MODE


#if EXPERIMENT == STEADY_TIME_MODE
const auto Epoch = std::chrono::steady_clock::now();
#endif


const spa_audio_info_raw OutputFormat =
{
    .format = SPA_AUDIO_FORMAT_F64,
    .rate = 48000,
    .channels = 1
};


struct SessionData
{
    pw_main_loop* Loop;
    pw_stream* Stream;
};


template<typename SampleT>
void OnProcessInner(SessionData* Session)
{
    pw_buffer* PBuffer = pw_stream_dequeue_buffer(Session->Stream);
    if (!PBuffer) {
        pw_log_warn("Out of buffers: %m");
        return;
    }

    spa_buffer* SBuffer = PBuffer->buffer;
    SampleT* OutSample = (SampleT*)(SBuffer->datas[0].data);

    if (!OutSample)
    {
        return;
    }

    const int Stride = sizeof(SampleT) * OutputFormat.channels;
    int Frames = SBuffer->datas[0].maxsize / Stride;
    if (PBuffer->requested)
    {
        Frames = SPA_MIN(PBuffer->requested, Frames);
    }

#if EXPERIMENT == FRAME_TIME_MODE
    static uint64_t FramesProcessed = 1;
    const double Interval = 1.0 / double(OutputFormat.rate);
#endif

    for (int Frame = 0; Frame < Frames; Frame++)
    {
#if EXPERIMENT == STEADY_TIME_MODE
        const std::chrono::duration<SampleT, std::ratio<60>> Offset = std::chrono::steady_clock::now() - Epoch;
        const SampleT Sample = Offset.count();

#elif EXPERIMENT == FRAME_TIME_MODE
        const SampleT Sample = double(FramesProcessed + Frame) * Interval / 60.0;
#endif

        for (int Channel = 0; Channel < OutputFormat.channels; Channel++)
        {
            *OutSample++ = Sample;
        }
    }

#if EXPERIMENT == FRAME_TIME_MODE
    FramesProcessed += Frames;
#endif

    SBuffer->datas[0].chunk->offset = 0;
    SBuffer->datas[0].chunk->stride = Stride;
    SBuffer->datas[0].chunk->size = Frames * Stride;

    pw_stream_queue_buffer(Session->Stream, PBuffer);
}


void OnProcess(void* UserData)
{
    OnProcessInner<double>((SessionData*)UserData);
}


static const pw_stream_events StreamEvents =
{
    .version = PW_VERSION_STREAM_EVENTS,
    .process = OnProcess,
};


int main(int argc, char *argv[])
{
    SessionData Session{ nullptr, nullptr };

    std::vector<const spa_pod*> Params;
    uint8_t SomeBuffer[1024];
    spa_pod_builder PodBuilder = SPA_POD_BUILDER_INIT(SomeBuffer, sizeof(SomeBuffer));

    pw_init(&argc, &argv);

    Session.Loop = pw_main_loop_new(nullptr);

    Session.Stream = pw_stream_new_simple(
        pw_main_loop_get_loop(Session.Loop),
        "a machine",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE, "Music",
            nullptr),
        &StreamEvents,
        &Session);

    {
        Params.push_back(spa_format_audio_raw_build(&PodBuilder, SPA_PARAM_EnumFormat, &OutputFormat));
    }

    {
        int Flags = PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS | PW_STREAM_FLAG_NO_CONVERT;
        pw_stream_connect(Session.Stream, PW_DIRECTION_OUTPUT, PW_ID_ANY, (pw_stream_flags)Flags, Params.data(), 1);
    }

    std::print("leave this running and also run b.out\n");
    pw_main_loop_run(Session.Loop);

    pw_stream_destroy(Session.Stream);
    pw_main_loop_destroy(Session.Loop);

    return 0;
}
