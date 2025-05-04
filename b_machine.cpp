#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

#include <print>
#include <vector>
#include <chrono>


const spa_audio_info_raw InputFormat =
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
    if (PBuffer == nullptr)
    {
        pw_log_warn("out of buffers: %m");
        return;
    }

    spa_buffer* SBuffer = PBuffer->buffer;
    SampleT* Samples = (SampleT*)(SBuffer->datas[0].data);
    if (Samples == nullptr)
    {
        return;
    }

    uint32_t Count = SBuffer->datas[0].chunk->size / sizeof(SampleT);


    static std::chrono::steady_clock::time_point LocalOrigin;
    static SampleT TransmissionStart = 0.0;

    auto LocalTime = std::chrono::steady_clock::now();
    SampleT TransmittedMinutes = Samples[Count - 1];

    static uint64_t FrameNumber = 0;


    //const std::chrono::duration<SampleT, std::ratio<60>> Offset = std::chrono::steady_clock::now() - Epoch;

    if (TransmissionStart == 0.0 && TransmittedMinutes > 0.0)
    {
        LocalOrigin = LocalTime;
        TransmissionStart = TransmittedMinutes;
    }
    else
    {
        const std::chrono::duration<SampleT, std::ratio<60>> LocalDelta = LocalTime - LocalOrigin;

        const SampleT LocalDeltaMinutes = LocalDelta.count();
        const SampleT MeasuredMinutes = TransmittedMinutes - TransmissionStart;
        const SampleT DriftMilliseconds = std::abs(MeasuredMinutes - LocalDeltaMinutes) * 60.0 * 1000.0;

        if ((++FrameNumber % 100) == 0)
        {
            std::print(
                "Stream({:.4f} s) - Clock({:.4f} s) = Drift({:.3f}) ms\n",
                MeasuredMinutes * 60.0,
                LocalDeltaMinutes * 60.0,
                DriftMilliseconds);
        }
    }

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
