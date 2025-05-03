#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

#include <print>
#include <vector>
#include <chrono>


const auto Epoch = std::chrono::steady_clock::now();


const spa_audio_info_raw InputFormat =
{
    .format = SPA_AUDIO_FORMAT_F32,
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
    if (PBuffer == nullptr)
    {
        pw_log_warn("out of buffers: %m");
        return;
    }

    spa_buffer* SBuffer = PBuffer->buffer;
    float* Samples = (float*)(SBuffer->datas[0].data);
    if (Samples == nullptr)
    {
        return;
    }

    uint32_t Count = SBuffer->datas[0].chunk->size / sizeof(float);

    static float LastSample = 0.0;
    for (int n = 0; n < Count; ++n)
    {
        float Sample = Samples[n];
        if (Sample != LastSample)
        {
            std::print("{}\n", Sample);
            LastSample = Sample;
        }
    }

    pw_stream_queue_buffer(Session->Stream, PBuffer);
}


void OnProcess(void* UserData)
{
    OnProcessInner<float>((SessionData*)UserData);
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
        "b machine",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE, "Music",
            PW_KEY_TARGET_OBJECT, "a machine",
            nullptr),
        &StreamEvents,
        &Session);

    {
        Params.push_back(spa_format_audio_raw_build(&PodBuilder, SPA_PARAM_EnumFormat, &InputFormat));
    }

    {
        int Flags = PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS | PW_STREAM_FLAG_NO_CONVERT;
        pw_stream_connect(Session.Stream, PW_DIRECTION_INPUT, PW_ID_ANY, (pw_stream_flags)Flags, Params.data(), 1);
    }

    std::print("connect \"a machine\" to \"b machine\" to start measuring latency drift\n");
    pw_main_loop_run(Session.Loop);

    pw_stream_destroy(Session.Stream);
    pw_main_loop_destroy(Session.Loop);

    return 0;
}
