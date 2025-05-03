
/*
 * This code was adapted from:
 * https://docs.pipewire.org/page_tutorial4.html
 */


#include <math.h>

#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

#include <limits>
#include <vector>

const double Tau = M_PI + M_PI;

const spa_audio_info_raw OutputFormat =
{
    .format = SPA_AUDIO_FORMAT_S32,
    .rate = 44100,
    .channels = 2
};


struct SessionData
{
    pw_main_loop* Loop;
    pw_stream* Stream;
    double Acc;
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


    for (int Frame = 0; Frame < Frames; Frame++)
    {
        Session->Acc += Tau * 440 / OutputFormat.rate;

        if (Session->Acc >= Tau)
        {
            Session->Acc -= Tau;
        }

        const double Range = double(std::numeric_limits<SampleT>::max());
        const SampleT Sample = SampleT(sin(Session->Acc) * Range * 0.7);
        for (int Channel = 0; Channel < OutputFormat.channels; Channel++)
        {
            *OutSample++ = Sample;
        }
    }

    SBuffer->datas[0].chunk->offset = 0;
    SBuffer->datas[0].chunk->stride = Stride;
    SBuffer->datas[0].chunk->size = Frames * Stride;

    pw_stream_queue_buffer(Session->Stream, PBuffer);
}

void OnProcess(void* UserData)
{
    OnProcessInner<int32_t>((SessionData*)UserData);
}


static const pw_stream_events StreamEvents =
{
    .version = PW_VERSION_STREAM_EVENTS,
    .process = OnProcess,
};


int main(int argc, char *argv[])
{
    SessionData Session{ nullptr, nullptr, 0.0 };

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
        int Flags = PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS;
        pw_stream_connect(Session.Stream, PW_DIRECTION_OUTPUT, PW_ID_ANY, (pw_stream_flags)Flags, Params.data(), 1);
    }

    pw_main_loop_run(Session.Loop);

    pw_stream_destroy(Session.Stream);
    pw_main_loop_destroy(Session.Loop);

    return 0;
}
