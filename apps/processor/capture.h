#include "apg/ConcurrentPool.h"
#include "apg/CudaWrappers.h"
#include "apg/DrawCommands.h"
#include "apg/Frame.h"
#include "apg/FramePipeline.h"
#include "apg/FrameProcessor.h"
#include "apg/Image.h"
#include "apg/ImageDisplayImgui.h"
#include "apg/LogWidget.h"
#include "apg/PlaybackControlUI.h"
#include "apg/PlaybackControlUIFactory.h"
#include "apg/PropertyTree.h"
#include "apg/PropertyTreeJson.h"
#include "apg/PropertyTreeWidget.h"
#include "apg/SigTermHandler.h"
#include "apg/SourceLoadingUtils.h"
#include "apg/SyncedMultiFrameSource.h"
#include "apg/Time.h"
#include "apg/WindowGlfwBgfxImgui.h"

#include "imgui.h"
#include "bgfx/bgfx.h"
#include "tracy/Tracy.hpp"
#include <mutex>
#include <utility>

#include "apg/IJpegEncoder.h"
#include "apg/FrameSource.h"

#include "apg/JpegDecodeProcessor.h"
#include "apg/JpegEncoderProcessor.h"
#include "apg/FrameDrawerProcessor.h"
#include "apg/CrashDumpSetup.h"
#include "apg/NATSConnection.h"
#include "apg/Sub.h"
#include "apg/Message.h"
#include "apg/FrameWriterProcessor.h"
#include "apg/TimestampInfo.h"
#include "apg/FlexbufferSerialisation.h"
#include "apg/FlexbufferSerialisationContainers.h"
#include "apg/SerialisationRegistry.h"

#include "apg/ImageResizeProcessor.h"
#include "apg/ResizeInfo.h"
#include "apg/FocusPeakingProcessor.h"
#include "apg/FocusPeakingWidget.h"

#include "LoopDataRepublisher.h"

#include "glog/logging.h"
#include "apg/CLIUtils.h"

#include "apg/CompressedImageBuffer.h"
#include "apg/ImageSerialization.h"

#include "apg/Synchronized.h"

#include "apg/CodecDefs.h"

#include "apg/AudioPlaybackProcessor.h"
#include "apg/AudioDataWriterProcessor.h"

#include "apg/FpsMeasurementProcessor.h"
#include "apg/HorusCameraControlWidget.h"

#include "apg/CallbackManager.h"
#include "apg/KLVProcessor.h"
#include "apg/KeyCommandMapper.h"
#include "apg/KlvDataPublisherProcessor.h"

#include "apg/SourceStatusWidget.h"
#include "apg/SourceFPSLogger.h"
#include "apg/NvencTvsWriterProcessor.h"
#include "apg/H26xTvsServerProcessor.h"
#include "apg/DecklinkOutputProcessor.h"

#include "imgui_internal.h"
#include "apg/CLIApp.h"
#include "apg/GUIApp.h"
#include "apg/VancHexViewerProcessor.h"

//#pragma GCC optimize ("O0")



namespace apg{

class MainWindow : public WindowGlfwBgfxImgui::WindowRenderer{
public:

    explicit MainWindow(PropNode node, const std::function<void()>& saveStateFn, const std::string &configurationJSONPath,
                        const RemoteStreamSettings &defaultRemoteStreamSettings,
                        ApplicationServicesPtr appServices,
                        std::shared_ptr<LogWidget> logWidget,
                        std::shared_ptr<HorusCameraControlWidget> horusCameraControlWidget)
    : node(node)
    , saveStateFn(saveStateFn)
    , configurationJSONPath(configurationJSONPath)
    , appServices(appServices)
    , defaultRemoteStreamSettings(defaultRemoteStreamSettings)
    , logWidget(logWidget)
    , horusCameraControlWidget(horusCameraControlWidget)
    , keyCommandMapper(appServices->getSharedObject<KeyCommandMapper>(""))
    , sourceStatusWidget(std::make_shared<SourceStatusWidget>(node->declareNode("Source Status")))
    , sourceFpsLogger(std::make_unique<SourceFPSLogger>())
    {
        auto callbackManager = appServices->getSharedObject<CallbackManager>("");
        callbackManager->addCallbackVoid(
                "Fullscreen",
                [this](){showExtraUi = !showExtraUi;},
                "Show/hide ui elements other than image window",
                {CallbackManager::KeyBinding{ImGuiKey_F}});
    }

    void init(int , int ) override {
        imageDisplayImgui = std::make_unique<ImageDisplayImgui>();
        propertyTreeWidget = std::make_unique<PropertyTreeWidget>(node, "tree", configurationJSONPath);
        vancHexViewerProcessor = appServices->getSharedObjectOrNull<VancHexViewerProcessor>();

        bgfx::frame();
    }

    bool update(int , int ) override {

        auto showExtraUiLocal = (bool)showExtraUi;

        ImGuiID dockSpaceId = ImGui::DockSpaceOverViewport(
                ImGui::GetMainViewport(),
                showExtraUiLocal ? ImGuiDockNodeFlags_None : (ImGuiDockNodeFlags) ImGuiDockNodeFlags_NoTabBar);

        if (playbackControl) {
            bool show = showExtraUiLocal;
            if (showExtraUiLocal) {
                ImGui::SetNextWindowDockID(dockSpaceId, ImGuiCond_FirstUseEver);
                show = ImGui::Begin("Playback");
            }

            playbackControl->draw(show);

            if (showExtraUiLocal) {
                ImGui::End();
            }
        }


        if (showExtraUiLocal) {
            ImGui::SetNextWindowDockID(dockSpaceId, ImGuiCond_FirstUseEver);
            ImGui::Begin("Property Tree");
            propertyTreeWidget->draw();
            ImGui::End();

            if (logWidget) {
                ImGui::SetNextWindowDockID(dockSpaceId, ImGuiCond_FirstUseEver);
                logWidget->draw();
            }

            ImGui::SetNextWindowDockID(dockSpaceId, ImGuiCond_FirstUseEver);
            ImGui::Begin("Clock");
            static int nudge = 0;
            ImGui::DragInt("nudge: " , &nudge, 5, -1000, 1000);
            auto time = timeToStringPretty(getTime() - int64_t(nudge * 1000));
            ImGui::Text("%s", time.c_str());
            ImGui::End();

            if (horusCameraControlWidget) {
                ImGui::SetNextWindowDockID(dockSpaceId, ImGuiCond_FirstUseEver);
                ImGui::Begin("Horus Camera Controls");
                horusCameraControlWidget->draw();
                ImGui::End();
            }

            ImGui::SetNextWindowDockID(dockSpaceId, ImGuiCond_FirstUseEver);
            ImGui::Begin("Key Commands");
            {
                keyCommandMapper->draw();
            }
            ImGui::End();

            {
                ImGui::SetNextWindowDockID(dockSpaceId, ImGuiCond_FirstUseEver);
                ImGui::Begin("Source Status");
                sourceStatusWidget->draw();
                ImGui::End();
            }

            {
                ImGui::SetNextWindowDockID(dockSpaceId, ImGuiCond_FirstUseEver);
                ImGui::Begin("Focus peaking");
                drawFocusPeakingWidget(node->declareNode("Focus peaking"), node->declareNode("Drawing"));
                ImGui::End();
            }

            if (vancHexViewerProcessor && showVancViewer) {
                ImGui::SetNextWindowDockID(dockSpaceId, ImGuiCond_FirstUseEver);
                ImGui::Begin("Vanc");
                vancHexViewerProcessor->draw();
                ImGui::End();
            }

        } else {
            keyCommandMapper->tick();
        }


        ImGui::SetNextWindowDockID(dockSpaceId, ImGuiCond_FirstUseEver);

        if (!showExtraUiLocal)
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));

        ImGui::Begin("Image");

        {
            auto lk = std::lock_guard(drawDataMutex);

            if (image && image->valid()) {
                imageDisplayImgui->setUseAnisotropicMagnification(useAnisotropicMagnification);
                auto interactionInfoForDraw = imageDisplayImgui->getInteractionInfoForDraw(image->getWidth(), image->getHeight(), false, !showExtraUiLocal);

                if (horusCameraControlWidget)
                    horusCameraControlWidget->drawImage(interactionInfoForDraw, *dcs);

                auto useZoomMap = zoomMapUiMode == 1 || (zoomMapUiMode == 0 && !showExtraUiLocal);

                // Zoom Map UI
                if (useZoomMap && !interactionInfoForDraw.fitToWindow){
                    // Outer Rect
                    auto imageAspectRatio = (float)interactionInfoForDraw.imageHeight / (float)interactionInfoForDraw.imageWidth;

                    float mapRectWidth = 200.f;
                    float mapRectHeight = mapRectWidth * imageAspectRatio;
                    auto pMin = ImVec2(interactionInfoForDraw.viewportTopLeftX + interactionInfoForDraw.viewportWidth - mapRectWidth - 25.f,
                                       interactionInfoForDraw.viewportTopLeftY + interactionInfoForDraw.viewportHeight - mapRectHeight - 25.f);
                    auto pMax = ImVec2(pMin.x + mapRectWidth, pMin.y + mapRectHeight);

                    // Inner-zoom rect
                    auto viewportAspectRatio = interactionInfoForDraw.viewportHeight / interactionInfoForDraw.viewportWidth;

                    float zoomRectWidth = interactionInfoForDraw.viewportWidth / ((double)interactionInfoForDraw.imageWidth * interactionInfoForDraw.scale) * mapRectWidth;
                    float zoomeRectHeight =  zoomRectWidth * viewportAspectRatio;
                    auto pMinZoom = ImVec2(pMin.x + (interactionInfoForDraw.topLeftViewportImageCoordinatesX / ((float)interactionInfoForDraw.imageWidth)) * mapRectWidth,
                                           pMin.y + (interactionInfoForDraw.topLeftViewportImageCoordinatesY / ((float)interactionInfoForDraw.imageHeight)) * mapRectHeight);
                    auto pMaxZoom = ImVec2(pMinZoom.x + zoomRectWidth, pMinZoom.y + zoomeRectHeight);

                    if (pMinZoom.x < pMin.x)
                        pMinZoom.x = pMin.x;
                    if (pMinZoom.y < pMin.y)
                        pMinZoom.y = pMin.y;
                    if (pMaxZoom.x > (pMin.x + mapRectWidth))
                        pMaxZoom.x = pMin.x + mapRectWidth;
                    if (pMaxZoom.y > (pMin.y + imageAspectRatio * mapRectWidth))
                        pMaxZoom.y = pMin.y + imageAspectRatio * mapRectWidth;

                    // Interaction
                    auto mousePos = interactionInfoForDraw.input.mousePosInViewPort;
                    mousePos->first += interactionInfoForDraw.viewportTopLeftX;
                    mousePos->second += interactionInfoForDraw.viewportTopLeftY;

                    bool drawExpandGraphic = false;
                    if (mousePos &&
                        (mousePos->first >= pMin.x) &&
                        (mousePos->first <= pMax.x) &&
                        (mousePos->second >= pMin.y - 25.f) && // expand for clicking text above rect
                        (mousePos->second <= pMax.y)) {

                        drawExpandGraphic = true;

                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                            interactionInfoForDraw.fitToWindow = true;
                    }

                    // Draw
                    imageDisplayImgui->drawWithInteractionInfo(interactionInfoForDraw, *image, *dcs, nullptr, false, {}, true);

                    ImDrawList* draw_list = ImGui::GetWindowDrawList();

                    draw_list->AddRect(pMin, pMax, IM_COL32(255, 0, 0, 255), 0.f, 0, 5.f);

                    if (pMaxZoom.x > pMinZoom.x && pMaxZoom.y > pMinZoom.y)
                        draw_list->AddRectFilled(pMinZoom, pMaxZoom, IM_COL32(0,255,0,120));

                    draw_list->AddText(ImGui::GetFont(), 25.f, ImVec2(pMin.x, pMin.y - 25.f), IM_COL32(255,0,0,255), "CLICK TO RESET VIEW", NULL, pMax.x - pMin.x);

                    if (drawExpandGraphic)
                        draw_list->AddRectFilled(pMin, pMax, IM_COL32(0, 225, 0, 55));

                    draw_list->AddRectFilled(ImVec2(pMin.x - 15.f, pMin.y - 25.f), ImVec2(pMax.x + 15.f, pMax.y + 15.f), IM_COL32(100,100,100,65));

                } else {
                    imageDisplayImgui->drawWithInteractionInfo(interactionInfoForDraw, *image, *dcs, nullptr, false, {}, useZoomMap || !showExtraUiLocal);
                }
            }
        }

        ImGui::End();

        if (!showExtraUiLocal)
            ImGui::PopStyleVar(1);

        if (addFramePeriodToTimeAdjustment) {
            addFramePeriodToTimeAdjustment = false;
            timeAdjustment = timeAdjustment + (int64_t)framePeriod;
        }
        if (subtractFramePeriodToTimeAdjustment) {
            subtractFramePeriodToTimeAdjustment = false;
            timeAdjustment = timeAdjustment - (int64_t)framePeriod;
        }

        if (saveState) {
            saveState = false;
            saveStateFn();
        }

        // std::this_thread::sleep_for(std::chrono::milliseconds(10));

        const auto [newDump, imagePath, error] = dump.withWLock([](auto& d){
            auto& [newDump, imagePath, error] = d;
            if (newDump) {
                newDump = false;
                return std::make_tuple(true, imagePath, error);
            }
            return d;
        });

        if (newDump)
            ImGui::OpenPopup("Image Dump");

        ImGui::SetNextWindowSize(ImVec2(400.f, 0.f));
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), 0, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Border, error ? 0xff0000ff : 0xff00ff00);
        if (ImGui::BeginPopup("Image Dump", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
            const auto titleText = std::string("Image Dump ") + (!error ? "Success" : "Failure");
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(titleText.c_str()).x) * 0.5f);
            ImGui::Text("%s", titleText.c_str());
            ImGui::Separator();
            if (!error)
                ImGui::TextWrapped("Dumped image to %s", imagePath.c_str());
            else
                ImGui::TextWrapped("Error: %s", error->c_str());
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor();



        return true;
    }

    FullScreenMode getFullScreenMode() const override {
        return showExtraUi ? WindowRenderer::FullScreenMode::WINDOWED : WindowRenderer::FullScreenMode::ONE_MONITOR;
    }

    RemoteStreamSettings getRemoteStreamSettings() const override {
        return RemoteStreamSettings{(int)remoteStreamingBitrate, (Codec)(int64_t)remoteStreamingCodec};
    }

    void draw(const Image &imageToDraw, const std::shared_ptr<DrawCommands> &newDcs, CudaStream streamToSyncTo){

        ZoneScoped;
        if(!imageToDraw.valid()) {
            LOG_EVERY_T(INFO, 10)<< "Being asked to draw invalid image!";
            return;
        }

        auto lk = std::lock_guard(drawDataMutex);

        if(!this->image || this->image->getType() != imageToDraw.getType()){
            image = std::make_unique<Image>(imageToDraw.getType(), imageToDraw.getWidth(), imageToDraw.getHeight());
        }

        image->create(imageToDraw.getType(), imageToDraw.getWidth(), imageToDraw.getHeight());

        if (imageToDraw.getType().isCuda()) {
            auto event = smartCudaEventCreate();
            cudaEventRecordWrap(event.get(), streamToSyncTo);
            cudaStreamWaitEventWrap(stream.get(), event.get());
            imageToDraw.copyTo(*this->image, stream.get());
        } else {
            imageToDraw.copyTo(*this->image);
        }

        if (newDcs)
            dcs = newDcs;
        else
            dcs->clear();
    }

    void setFrameBasedPlaybackControl(const std::shared_ptr<PlaybackControlUI> &newPlaybackControl) {
        playbackControl = newPlaybackControl;
    }

    FrameDrawerProcessor::DumpCallback getDumpCallback() {
        return [this](const std::string &imagePath, const std::optional<std::string>& error) {
            dump.withWLock([imagePath, error](auto& d){ d = {true, imagePath, error}; });
        };
    }

//
//    void setAverageMaxWriteTime(ResizeInfo ri, std::pair<int64_t, int64_t> am) {
//        averageMaxWriteTimes.withWriteLock([&](std::map<ResizeInfo, std::pair<int64_t, int64_t>> &amwts){
//            amwts[ri] = am;
//        });
//    }

    void addSourceStatus(const std::string& id, std::weak_ptr<FrameSourceStatus> status, bool liveSource) {
        sourceStatusWidget->add(id, status, liveSource, true);

        if (sourceFpsLogger) {
            sourceFpsLogger->addSource(id, status);
        }
    }

    ~MainWindow() override= default;

private:
    PropNode node;
    PropVarBool saveState = PropVarBool(node, "save configuration", false);

    PropVarInt timeAdjustment = {node, "time adjustment", 0, IntAttributes(std::nullopt, std::nullopt, 1, "Added to all timestamps. In microseconds.")};
    PropVarInt framePeriod = {node, "frame period", 20000, IntAttributes("In microseconds, used when adding or subtracting frames from the timeAdjustment")};
    PropVarBool addFramePeriodToTimeAdjustment = {node, "add frame period to time adjustment", false};
    PropVarBool subtractFramePeriodToTimeAdjustment = {node, "subtract frame period to time adjustment", false};
    PropVarBool showExtraUi = {node, "showExtraUi", true, BoolAttributes{"Show property tree widget, log widget and view selection radio buttons. Press `f` to toggle this"}};
    PropVarBool useAnisotropicMagnification = {node, "Use Anisotropic Magnification", false};
    PropVarInt zoomMapUiMode = {node, "zoomMapUiMode", 0, IntAttributes(std::vector<std::string>{"On when fullscreen", "On", "Off"})};
    PropVarBool showVancViewer = {node, "showVancViewer", false, BoolAttributes{"Show the VANC hex viewer"}};
    std::function<void()> saveStateFn;
    const std::string configurationJSONPath;
    ApplicationServicesPtr appServices;
    const RemoteStreamSettings defaultRemoteStreamSettings;
    PropVarInt remoteStreamingBitrate =
            {node, "remoteStreamingBitrate", defaultRemoteStreamSettings.bitrate,
             IntAttributes(0, {}, 100000, "In bits per 30 frames, not including audio or other metadata",
                           NumericInputType::DRAG)};

    PropVarInt remoteStreamingCodec =
            {node, "remoteStreamingCodec", (int64_t)defaultRemoteStreamSettings.codec,
             IntAttributes(std::vector<std::string>{"H264", "HEVC"})};

    std::mutex drawDataMutex;
    std::unique_ptr<Image> image;
    std::shared_ptr<DrawCommands> dcs = std::make_shared<DrawCommands>();

    std::unique_ptr<ImageDisplayImgui> imageDisplayImgui;
    std::unique_ptr<PropertyTreeWidget> propertyTreeWidget;

    SmartCudaStream stream = smartCudaStreamCreate();

    std::shared_ptr<PlaybackControlUI> playbackControl;
    std::shared_ptr<LogWidget> logWidget;

    Synchronized<std::tuple<bool, std::string, std::optional<std::string>>> dump;

    std::shared_ptr<HorusCameraControlWidget> horusCameraControlWidget;

//    Synchronized<std::map<ResizeInfo, std::pair<int64_t, int64_t>>> averageMaxWriteTimes;

    std::shared_ptr<KeyCommandMapper> keyCommandMapper;

    std::shared_ptr<SourceStatusWidget> sourceStatusWidget;
    std::unique_ptr<SourceFPSLogger> sourceFpsLogger;
    std::shared_ptr<VancHexViewerProcessor> vancHexViewerProcessor;
};

class AppServicesImpl : public FrameProcessorApplicationServices{
public:
    AppServicesImpl(const std::string& imageDumpPrefix)
        : imageDumpPrefix(imageDumpPrefix){

    }

    void requestReprocess() override {

    }

    std::optional<std::string> requestPath(const std::string& pathId) override {
        if (pathId == "imageDumpPrefix")
            return imageDumpPrefix;
        else
            return std::nullopt;
    }

    std::string imageDumpPrefix;
};
class CaptureApp : public GUIApp, public CLIApp {
public:
    void registerExtraArguments(CLI::App &cli) override {
        cli.add_option("--configuration", configurationJSONPath, "Load in property tree configuration from JSON.");
        cli.add_option("source-url,--source-url", sourceUrls)->required(true);
        cli.add_option("--feed-name", feedName, "Video feed name");
        cli.add_option("--audio-stream-names", audioStreamNames, "Audio stream names. A stream is a collection of channels that go together. eg PGM might consist of a left and a right channel")->delimiter(',');
        cli.add_option("--video-server-feed-type", videoServerFeedType, "Video feed type");
        cli.add_option( "--nats-url", brokerURL, "NATS broker url");
        cli.add_option( "--nats-discovery-url", announceURL, "NATS url for announcing services for discovery by other apps");
        cli.add_option("--rotation-over-90", rotationOver90, "Rotate input feed over 90 degrees clockwise by X times.");

        auto loopSimulateLiveOption =
                cli.add_flag("--loop-simulate-live", loopSimulateLive,
                             "When playing from a time based source (tfs, tvs) map time range of data (or provided "
                             "loopTimeStart loopTimeEnd) to all timelooping - useful for testing with data as if live");
        cli.add_option("--loop-time-start", loopTimestampStart,
                       "Start time of looping modes. Timestamp needs to be in this style: 2021-04-15_18-52-48-323396")
                ->needs("--loop-simulate-live");
        cli.add_option("--loop-time-end", loopTimestampEnd, "End time of loop - see --loop-time-start")
                ->needs("--loop-simulate-live");
        loopSimulateLiveOption->needs("--loop-time-start")->needs("--loop-time-end");

        cli.add_option("--record", record, "whether to record the feed and hang a TVS server.")->needs("--feed-name");
        // cli.add_option("--streaming-input-known-low-latency", knownOneFramePerPacketLowLatency);
        cli.add_option("--max-num-frames-per-store", maxNumFramesPerStore, "Defaults to 60*60*5 = 5 minutes at 60fps");
        cli.add_option("--max-num-stores", maxNumStores, "Defaults to 30 = 2.5 hours at 5 minutes per file");

        cli.add_flag("--create-re-encoded-video-publisher", createReEncodedVideoPublisher, "Enable this to create a zmq publisher for the captured data after it has been re-encoded (probably to jpeg). Currently no way to use Nats for this. Uses --re-encoded-video-pub-port or 4224 for the port");
        cli.add_option("--re-encoded-video-pub-port", reEncodedVideoPublisherPortNum, "Port number to use for ZMQ publishing of re-encoded video");

        cli.add_flag("--create-h26x-publisher", createH26xPublisher,
                     "Enable this to create a zmq publisher for the raw captured data. Currently no way to use Nats for "
                     "this. Uses --h26x-pub-port or 4223 for the port")
                ->needs("--feed-name");

        cli.add_option("--h26x-pub-port", h26xPublisherPortNum, "Port number to use for ZMQ publishing of h26x data")
                ->needs("--create-h26x-publisher");

        cli.add_flag("--create-h26x-server", createH26xServer,
                     "Enable this to create a tvs server for the raw captured data")
                ->needs("--feed-name");

        cli.add_flag("--create-pipeline-h26x-server", createPipelineH26xServer,
                     "Allows creating a server for h26x sources such as ZMQ")
                ->needs("--feed-name");

        cli.add_flag("--create-h26x-encoder", createH26xEncoder,
                     "Enable this to create a h26x encoder for the raw video input such as SDI");

        cli.add_option("--subsample-widths", subsampleImageWidths, "Specify widths for subsampled images. For example --subsample-widths=1000,500 will produce images that are 1000 and 500 pixels wide")->delimiter(',');
        cli.add_option("--subsample-scale-factors", subsampleScaleFactorsStrings,
                       "Specify subsample levels as fractions. For example --subsample-scale-factors=1:2,2:3,3:4 will produce images that are 1/2, 2/3 and 3/4 the size of the original image")->delimiter(',');
        cli.add_option("--subsample-skip-rate", subsampleSkipRate, "Specify how many frames to skip between subsampled frames");

        cli.add_flag("--use-hevc-sei-timestamp", useHevcSEITimestamp, "Legacy CLA. Use --use-sei-timestamp instead");
        cli.add_flag("--use-sei-timestamp", useSEITimestamp, "When capturing an hevc stream using FFmpeg, use the timestamp embedded in the \"Supplemental Enhancement Information\"");
        cli.add_option("--jpeg-quality", jpegQualityOverride, "Override the default quality in the property tree");

        cli.add_option("--restream-url", restreamUrl);

        cli.add_option("--frame-pool-size", framePoolSize, "The number of frames to put in the frame pool on startup. Needs to be enough to keep up with live processing, but fewer is better to prevent latency spikes and high memory usage.");

        cli.add_option("--jpeg-target-bits-per-pixel", targetBitsPerPixel, "Target number of bits per pixel for the Jpeg encoder");

        cli.add_option("--num-frames-per-h26x-file", numFramesPerH26xFile, "Defaults to 60*60*5 = 5 minutes at 60fps");
        cli.add_option("--num-h26x-files", numH26xFiles, "Defaults to 12 * 12 = 12 hours at 5 minutes per file");

        cli.add_option("--remote-stream-bit-rate", remoteStreamBitrate, "Bit rate for streaming to Remote in bps");

        cli.add_option("--image-dump-prefix", imageDumpPrefix, "Add a prefix to image dumps");
        cli.add_option("--max-stores-bytes-size", maxStoresBytesSize, "Max size in bytes for the stores (default to 0 as in disabled)");

        cli.add_option("--horus-camera-control-url", horusCameraControlUrl, "URL for Horus camera control");

        cli.add_option("--republish-as-klv", republishAsKlv, "Republish H265 data in KLV packets over NATS, topic klv.klv-video.<feed_name>")->needs("--feed-name");
        cli.add_option("--klv-republish-broker-url", klvRepublishBrokerUrl, "NATS Broker URL to publish KLV packets over")->needs("--republish-as-klv");

        cli.add_option("--use-scrubbable-h26x", useScrubbableH26x, "Uses a scrubbable h26x source, will use more memory");
        cli.add_flag("--use-scrubbable-gpu-cache", useScrubbableGpuCache, "Cache scrubbable frames in GPU instead of CPU");
        cli.add_option("--encode-full-size-jpeg", encodeFullSizeJpeg, "Encode full size jpeg, disable when using h26x as output source");

        cli.add_flag("--create-decklink-output", createDecklinkOutput, "Create a decklink output for the video feed");

        cli.add_option("--backup-url", backupUrl, "If a streaming feed becomes unavailable, this will be switched to");

        cli.add_option("--start-time", playbackStartTimeString, "Start time for playback. If publishing playback instructions the playback will start paused at this time.");

        cli.add_option("--ccam-adjust-timestamp-with-exposure", ccamAdjustTimestampWithExposure, "Use the ccam exposure to adjust the timestamp");

        cli.add_flag("--publish-encoded-h26x-over-nats", publishEncodedH26xOverNats, "Publish encoded h26x over NATS");
        cli.add_flag("--publish-encoded-h26x-over-zmq", publishEncodedH26xOverZMQ, "Publish encoded h26x over ZMQ");
        cli.add_option("--encoded-h26x-zmq-pub-port", encodedH26xZmqPubPort, "Port number to use for ZMQ publishing of h26x data")
                ->needs("--publish-encoded-h26x-over-zmq");

        cli.add_flag("--disable-decoding-stream", disableDecodingStream, "Disable decoding stream ");

        cli.add_flag("--allow-future-frame-times", allowFutureFrameTimes, "Don't skip frames with frame times that look like they are in the future");
        cli.add_flag("--allow-backwards-time-jumps", allowBackwardsTimeJumps, "Don't skip frames with frame times that have gone backwards");

        cli.add_flag("--combine-sources", combineSources, "eg for Decklink SSM");
        cli.add_option("--deinterlace-combined-sources", deinterlaceCombinedSources, "eg for Decklink SSM");

        cli.add_flag("--force-cpu-decode", forceCpuDecodeAllSources, "Force CPU decode for all sources");
        cli.add_flag("--force-cpu-decode-jpeg", forceCpuDecodeJpegSources, "Force CPU decode for jpeg sources");
        cli.add_flag("--force-cpu-decode-h26x", forceCpuDecodeH26xSources, "Force CPU decode for h26x sources");


        playbackControlUISettings = PlaybackControlUIFactory::registerExtraArguments(cli);

        decklinkOutputOptions.registerArguments("decklink-output", cli);
    }

    std::string getName() const override {
        return "Capture";
    }

    PropNode getRootPropNode() const override {
        return propNode;
    }

    std::shared_ptr<MainWindow> initApp() {
        useSEITimestamp = useSEITimestamp || useHevcSEITimestamp;

        std::shared_ptr<LogWidget> logWidget = createGLogWidget(true);

        //
        // Setup synced multi frame source. Used for playback control of time based sources.
        //
        if(loopSimulateLive) {
            CHECK(!loopTimestampStart.empty() && !loopTimestampEnd.empty());

            //
            // Extract range or error out if user failed to specify properly
            //
            std::optional<std::pair<int64_t, int64_t>> range;
            auto startTime = timeFromString(loopTimestampStart);
            auto endTime = timeFromString(loopTimestampEnd);

            if (!startTime) {
                LOG(ERROR) << "Could not parse loop-start-time " << loopTimestampStart;
                return nullptr;
            }

            if (!endTime) {
                LOG(ERROR) << "Could not parse loop-end-time " << loopTimestampEnd;
                return nullptr;
            }

            range = std::make_pair(startTime.value(), endTime.value());

            if (range->first > range->second) {
                LOG(ERROR) << "Loop start/end swapped!";
                return nullptr;
            }

            syncedMultiFrameSource->setLoopSimulateLive(true, range);
        }

        if (!playbackStartTimeString.empty()) {
            playbackControlUISettings->startTimeVideo = timeFromString(playbackStartTimeString);

            if (!playbackControlUISettings->startTimeVideo) {
                LOG(ERROR) << "Issue parsing --start-time argument: " << playbackStartTimeString;
                return nullptr;
            }
        }

        auto drawImage = std::make_shared<Image>();
        PropVarString sourceUrlDisplay = {propNode, "sourceUrl", "(not set)", StringAttributes::createDisplayOnly("sourceInfo.url of the capture's source")};

        const auto loadPropStateMessage = propNodeFromJsonFile(configurationJSONPath, propNode, false);
        if (loadPropStateMessage)
            LOG(ERROR) << "Could not load property tree state from file: " << *loadPropStateMessage;
        else
            LOG(INFO) << "Property tree state loaded successfully from: " << configurationJSONPath;


        appServices = std::make_shared<AppServicesImpl>(imageDumpPrefix);

        auto callbackManager = std::make_shared<CallbackManager>();
        auto keyCommandMapper = std::make_shared<KeyCommandMapper>(callbackManager);
        appServices->injectSharedObject(callbackManager);
        appServices->injectSharedObject(keyCommandMapper);

        auto saveStateFn = [this] {
            auto path = std::filesystem::path(configurationJSONPath);

            if (path.parent_path().string() != "") {
                std::error_code ec;
                std::filesystem::create_directories(path.parent_path(), ec);
                if (ec)
                    LOG(ERROR) << "Could not create directory " << path.parent_path() << ": " << ec.message();
            }

            const auto saveMessage = propNodeToJsonFile(configurationJSONPath, propNode, false);
            if (saveMessage)
                LOG(ERROR) << *saveMessage;
            else
                LOG(INFO) << "Property tree state saved successfully to: " << configurationJSONPath;
        };

        std::shared_ptr<HorusCameraControlWidget> horusCameraControlWidget;
        if (!horusCameraControlUrl.empty()) {
            horusCameraControlWidget = createHorusCameraControlWidget(horusCameraControlUrl, propNode->declareNode("Horus"), HorusCameraControlWidget::Settings{
                (announceURL && !feedName.empty()) ? std::make_shared<NATSConnection>(*announceURL) : nullptr,
                feedName
            });
        }

        auto mainWindow = std::make_shared<MainWindow>(propNode, saveStateFn, configurationJSONPath,
                WindowGlfwBgfxImgui::WindowRenderer::RemoteStreamSettings{remoteStreamBitrate, Codec::HEVC},
                appServices, logWidget, horusCameraControlWidget);

        auto source = pipeline.createSourceNode();

        auto fpsMeasurementNode = pipeline.createProcessingNode(
                createFpsMeasurementProcessor(propNode->declareNode("Fps measurement"), appServices));

        std::function<std::shared_ptr<IJpegDecoder>()> getDecoder = nullptr;
        if (forceCpuDecodeJpegSources || forceCpuDecodeAllSources) {
            getDecoder = [decoderPool = std::make_shared<Synchronized<ConcurrentPool<IJpegDecoder>>>()]() {
                return decoderPool->wlock()->takeFromInfinitePool([](){ return createTurboJpegDecoder();});
            };
        }

        auto jpegDecoderNode = pipeline.createProcessingNode([this, getDecoder](){
            return createJpegDecodeProcessor(appServices, nullptr, getDecoder);
        }, 8);

        std::vector<ResizeInfo> resizeInfoVec = {ResizeInfo(ResizeInfo::ResizeMode::NONE)};

        for (auto &w : subsampleImageWidths)
            resizeInfoVec.emplace_back(ResizeInfo(ResizeInfo::ResizeMode::TARGET_WIDTH, w));

        for (auto &s : subsampleScaleFactorsStrings) {
            auto colonPos = s.find_first_of(":");

            if (colonPos == std::string::npos)
                continue;

            auto numerator = std::stoi(s.substr(0, colonPos));
            auto denominator = std::stoi(s.substr(colonPos + 1));

            resizeInfoVec.emplace_back(ResizeInfo(ResizeInfo::ResizeMode::SINGLE_SCALEFACTOR, numerator, denominator));
        }

        auto imageResizeNode = pipeline.createProcessingNode([resizeInfoVec, this]() -> std::shared_ptr<FrameProcessor> {
            return createImageResizeProcessor(
                    std::make_pair(ImageType(StandardImageTypes::RGB8_INTERLEAVED).cuda(0), ""),
                    resizeInfoVec, ResizeImageCudaQuality::HIGH, subsampleSkipRate);
        }, 8);

        std::vector<ImageToDrawInfo> imagesToSelect = {
                {ImageType(StandardImageTypes::RGB8_INTERLEAVED).cuda(0), "", "Video"},
                {ImageType(StandardImageTypes::RGB8_INTERLEAVED), "", "Video2"},
                {ImageType(StandardImageTypes::RGB8_INTERLEAVED).cuda(0), "nothing", "Nothing", {}},
                {ImageType(StandardImageTypes::RGB8_INTERLEAVED).cuda(0), "FocusPeakingResult", "Focus Peaking Filter", {}},
        };

        auto displayNode = pipeline.createProcessingNode(
                std::make_shared<FrameDrawerProcessor>(appServices,
                                              propNode->declareNode("Drawing"),
                                              [mainWindowPtr = std::weak_ptr<MainWindow>(mainWindow)](const Image &image, const std::shared_ptr<DrawCommands> &dcs, CudaStream stream, const std::map<std::string, std::any> &, const std::vector<apg::FrameDrawerProcessor::DynamicDrawCallback> &) {
                                                  if (auto mainWindow = mainWindowPtr.lock())
                                                      mainWindow->draw(image, dcs, stream);
                                              }, feedName, std::vector<std::string>{}, imagesToSelect, mainWindow->getDumpCallback()));

        auto focusPeakingNode = pipeline.createProcessingNode(createFocusPeakingProcessor(appServices, propNode->declareNode("Focus peaking")));

        auto audioPlaybackNode = pipeline.createProcessingNode(createAudioPlaybackProcessor(propNode->declareNode("Audio playback"), appServices));

        //
        // Wrap H265 in KLV and publish
        //
        if (republishAsKlv) {
            auto klvProcessorNode = pipeline.createProcessingNode(std::make_shared<KLVProcessor>(appServices, propNode->declareNode("KLV Processor")));
            auto klvPublishingNatsConnection = std::make_shared<NATSConnection>(klvRepublishBrokerUrl);
            auto klvPublisherNode = pipeline.createProcessingNode(std::make_shared<KlvDataPublisherProcessor>(appServices, propNode->declareNode("KLV Publishing"), klvPublishingNatsConnection, feedName, "klv-video"));
            pipeline.connect(source->getOutput(), klvProcessorNode->getInput());
            pipeline.connect(klvProcessorNode->getOutput(), klvPublisherNode->getInput());
        }

        pipeline.connect(source->getOutput(), fpsMeasurementNode->getInput());

        if (createPipelineH26xServer){
            H26xTvsServerProcessorOptions h26XTvsServerProcessorOptions {
                    .urlCaptureTVSStorePath = "data/h26xstream/" + feedName + "_",
                    .numFramesPerH26xFile = 60 * 60 * 5,
                    .numH26xFiles = 12 * 12,
                    .maxStoresBytesSize = 0,
                    .writeToDiskIfPathProvided = true,
                    .createH26xServer = true,
                    .createH26xPublisher = createH26xPublisher,
                    .outputFeedName = feedName,
                    .natsServerConnection = std::make_shared<NATSConnection>(brokerURL),
                    .natsServerAnnounceConnection = announceURL ? std::make_shared<NATSConnection>(*announceURL) : nullptr};

            auto h26xServerNode = pipeline.createProcessingNode(
                    createH26xTvsServerProcessor(propNode->declareNode("H26x TVS Server"), appServices, h26XTvsServerProcessorOptions));
            propNode->getNode("H26x TVS Server")->overrideDefault("enabled", true);
            pipeline.connect(fpsMeasurementNode->getOutput(), h26xServerNode->getInput());
            pipeline.connect(h26xServerNode->getOutput(), jpegDecoderNode->getInput());
        }
        else{
            pipeline.connect(fpsMeasurementNode->getOutput(), jpegDecoderNode->getInput());
        }


        pipeline.connect(jpegDecoderNode->getOutput(), imageResizeNode->getInput());
        pipeline.connect(imageResizeNode->getOutput(), focusPeakingNode->getInput());

        pipeline.connect(focusPeakingNode->getOutput(), displayNode->getInput());


        if (createDecklinkOutput) {
            auto decklinkOutputProcessor = pipeline.createProcessingNode(createDecklinkSdiOutputProcessor(propNode->declareNode("Decklink Output"), appServices, decklinkOutputOptions));
            pipeline.connect(source->getOutput(), decklinkOutputProcessor->getInput());
            pipeline.connect(decklinkOutputProcessor->getOutput(), audioPlaybackNode->getInput());
        } else {
            pipeline.connect(source->getOutput(), audioPlaybackNode->getInput());
        }

        auto vancHexViewer = createVancHexViewerProcessor(propNode->declareNode("VANC Hex Processor"), appServices);
        auto vancHexProcessorNode = pipeline.createProcessingNode(vancHexViewer);
        pipeline.connect(audioPlaybackNode->getOutput(), vancHexProcessorNode->getInput());
        appServices->injectSharedObject(vancHexViewer);

        auto preProcessOutput = focusPeakingNode->getOutput();

        auto serialisationRegistry = std::make_shared<SerialisationRegistry>();
        serialisationRegistry->registerTypeWithFlexbufferSerialisation<TimestampInfo>("apg::TimestampInfo");
        serialisationRegistry->registerTypeWithFlexbufferSerialisation<CompressedImageBuffer>("apg::CompressedImageBuffer");

        if (createH26xEncoder){
            NvencTvsWriterProcessorOptions options;
            if (publishEncodedH26xOverNats){
                options.publishNatsConnection = std::make_shared<NATSConnection>(brokerURL);
            }

            if (publishEncodedH26xOverZMQ) {
                options.publishZmq = true;
                options.zmqPortNumber = encodedH26xZmqPubPort;
            }

            options.feedName = feedName;

            options.serverNatsConnection = std::make_shared<NATSConnection>(brokerURL);
            options.natsAnnounceConnection = announceURL ? std::make_shared<NATSConnection>(*announceURL) : nullptr;

            auto encoder = pipeline.createProcessingNode(createNvencTvsWriterProcessor(propNode->declareNode("H26x Encoder"), appServices, options));
            pipeline.connect(preProcessOutput, encoder->getInput());
            preProcessOutput = encoder->getOutput();
        }

        if (record) {
            CHECK(!feedName.empty());
            std::vector<std::tuple<ImageType, std::string, std::string>> imagesToEncode = {
                {ImageType(StandardImageTypes::RGB8_INTERLEAVED).cuda(0), "", "jpeg"}
            };

            auto jpegEncodePropNode = propNode->declareNode("JpegEncodeProcessor");
            auto jpegQualityControllerPropNode = jpegEncodePropNode->declareNode("Quality Controller");
            auto jpegQualityController = createJpegEncoderQualityController(jpegQualityControllerPropNode, !targetBitsPerPixel.has_value());

            if (jpegQualityOverride) jpegQualityControllerPropNode->overrideDefault("jpeg quality", static_cast<int64_t>(*jpegQualityOverride));
            if (targetBitsPerPixel){
                jpegQualityControllerPropNode->overrideDefault("enabled", true);
                jpegQualityControllerPropNode->overrideDefault("target bits per pixel", *targetBitsPerPixel);
            }

            auto encoderNode = pipeline.createProcessingNode([this, jpegEncodePropNode, imagesToEncode, jpegQualityController, encodeFullSize=encodeFullSizeJpeg]() {
                return createJpegEncodeProcessor(jpegEncodePropNode, jpegQualityController, appServices, imagesToEncode, [](){return true;}, encodeFullSize);
            }, 8);

            LOG(INFO) << "Creating video encoder and server pipeline for feed name " << feedName;
            pipeline.connect(preProcessOutput, encoderNode->getInput());

            auto lastOutput = encoderNode->getOutput();

            for (auto &ri : resizeInfoVec) {
                // We save "loopCrossingProcessed" if they are there for simulate live looped reprocessing mode.
                // (This is now going to become legacy and can be removed soon 2021-09-13 LD


                if (!encodeFullSizeJpeg && ri.mode == ResizeInfo::ResizeMode::NONE){
                    continue;
                }

                auto valuesToSave = std::vector<std::string>{ri.toString("jpeg"), "originalImageSize", "loopCrossingsProcessed", "timestampInfo", "measuredFps", "usingBackupUrl"};
                auto requiredValuesToSave = std::vector<std::string>{};

                if (ri.mode != ResizeInfo::ResizeMode::NONE) {
                    requiredValuesToSave.push_back(ri.toString("jpeg"));
                }


                auto imagesToSave = std::vector<std::pair<ImageType, std::string>>{};

                auto videoServerFeedTypeForSubLevel = ri.toString(videoServerFeedType);

                auto frameWriterProcessorNode = propNode->declareNode("FrameWriterProcessor for " + feedName + "." + videoServerFeedTypeForSubLevel);

                auto frameWriterProcessor = pipeline.createProcessingNode(
                        std::make_shared<FrameWriterProcessor>(
                                feedName,
                                videoServerFeedTypeForSubLevel,
                                valuesToSave,
                                imagesToSave,
                                serialisationRegistry,
                                std::make_shared<NATSConnection>(brokerURL),
                                appServices,
    //                            [mainWindow, ri](int64_t average, int64_t max) { mainWindow->setAverageMaxWriteTime(ri, std::make_pair(average, max));},
                                frameWriterProcessorNode,
                                announceURL ? std::make_shared<NATSConnection>(*announceURL) : nullptr,
                                FrameWriterProcessorOptions{
                                    createReEncodedVideoPublisher && ri.mode == ResizeInfo::ResizeMode::NONE,
                                    reEncodedVideoPublisherPortNum, {}, false, false,
                                    maxNumFramesPerStore, maxNumStores, maxStoresBytesSize, "data/apg/", requiredValuesToSave})
                );

                frameWriterProcessorNode->declareBool("logMaxAndAverageWriteTimes", true, BoolAttributes("")); // override default value

                pipeline.connect(lastOutput, frameWriterProcessor->getInput());
                lastOutput = frameWriterProcessor->getOutput();
            }
        }

        if (!audioStreamNames.empty()) {
            std::map<int, std::string> audioStreamNamesMap;
            for (size_t i = 0; i < audioStreamNames.size(); ++i)
                audioStreamNamesMap[i] = audioStreamNames[i];

            auto audioDataWriterProcessor = pipeline.createProcessingNode(
                    createAudioDataWriterProcessor(
                            propNode->declareNode("AudioDataWriterProcessor"),
                            appServices,
                            audioStreamNamesMap,
                            std::make_shared<NATSConnection>(brokerURL),
                            announceURL ? std::make_shared<NATSConnection>(*announceURL) : nullptr,
                            record,
                            maxStoresBytesSize)
            );

            pipeline.connect(preProcessOutput, audioDataWriterProcessor->getInput());
        }

        pool.setCleanup([](Frame *f){
          f->cleanup();
        });

        for(int i = 0; i < framePoolSize; ++i)
            pool.add(createFrameUniquePtr());

        std::string sourceId;


        if (!feedName.empty()) {
            timeSyncAdjustmentSub = createNatsSubscriber(
                    std::make_shared<NATSConnection>(announceURL.value_or(brokerURL)),
                    "newSyncOffset." + feedName,
                    [this, saveStateFn](std::unique_ptr<Message> m, const std::string &) {
                        auto messageStr = m->asString();
                        auto j = nlohmann::json::parse(messageStr);
                        auto newOffset = j.value("newOffset", (int64_t)timeAdjustment);
                        timeAdjustment = newOffset;
                        saveStateFn();
            });
        }

        {
            CreateFrameSourceOptions options;
            options.ccamAdjustTimestampWithExposure = ccamAdjustTimestampWithExposure;
            options.rotationOver90 = rotationOver90;
            options.useSEITimestamp = useSEITimestamp;
            options.restreamUrl = restreamUrl;
            options.numFramesPerH26xFile = numFramesPerH26xFile;
            options.numH26xFiles = numH26xFiles;
            options.useScrubbableH26xStore = useScrubbableH26x;
            options.useScrubbableGpuCache = useScrubbableGpuCache;
            options.disableDecodingStream = disableDecodingStream;
            options.allowFutureFrameTimes = allowFutureFrameTimes;
            options.allowBackwardsTimeJumps = allowBackwardsTimeJumps;
            options.combineSources = combineSources;
            options.deinterlaceCombinedSources = deinterlaceCombinedSources;
            options.propNode = propNode->declareNode("Source");

            options.forceCpuDecode = forceCpuDecodeAllSources || forceCpuDecodeH26xSources;
            options.doCudaUpload = !forceCpuDecodeAllSources && !forceCpuDecodeH26xSources;

            if (record) {
                CHECK(!feedName.empty());
                options.urlCaptureTVSStorePath = "data/h26xstream/" + feedName + "_";
            }

            options.maxStoresBytesSize = maxStoresBytesSize;
            if (createH26xPublisher) {
                // setting this will create a publisher in the FFMpegDemuxerFrameSource so that the feed can be
                // immediately republished with minimal latency
                CHECK(!feedName.empty());
                options.publish = createH26xPublisher;
                options.zmqPubPortNum = h26xPublisherPortNum;
            }

            if (createH26xServer) {
                CHECK(!feedName.empty());
                options.createH26xServer = true;
                options.natsServerConnection = std::make_shared<NATSConnection>(brokerURL);
                options.natsServerAnnounceConnection =
                        announceURL ? std::make_shared<NATSConnection>(*announceURL) : nullptr;
            }

            if (!feedName.empty()) {
                options.outputFeedName = feedName;
                options.isSourceActiveCallback = [](const std::string &){
                    return true;
                };
            }

            options.getTimeAdjustment = [this](){ return (int64_t)timeAdjustment; };
            options.frameFromBytesMap = {
                {"nascar-active-run", [](const uint8_t* data, size_t size, FramePtr frame) {
                    frame->setValue("activeRun", std::string(data, data + size));
                }},
            };

            PropVarBool pauseCapture = {propNode, "pauseCapture", false, BoolAttributes::createNoSave("")};
            options.shouldPauseFn = [pauseCapture]() {
                return (bool)pauseCapture;
            };

            if (backupUrl) {
                options.backupUrl = backupUrl;
                PropVarBool usingBackupUrl = {propNode, "usingBackupUrl", false, BoolAttributes::createDisplayOnly("")};
                PropVarBool switchUrlSource = {propNode, "switchUrlSource", false, BoolAttributes::createNoSave("")};
                options.shouldSwitchFn = [usingBackupUrl, switchUrlSource](bool currentlyUsingBackup)mutable {
                    usingBackupUrl = currentlyUsingBackup;
                    if (switchUrlSource) {
                        switchUrlSource = false;
                        return true;
                    }
                    return false;
                };
            }

            std::vector<std::string> unusedInputs;
            auto sources = loadSources(sourceUrls, serialisationRegistry, syncedMultiFrameSource.get(), options, unusedInputs);


            for(const auto &[id, source] : sources){
                LOG(INFO) << "Found source ID " << id;
            }

            for(const auto& unused : unusedInputs){
                LOG(ERROR) << "Unused input: " << unused;
            }

            if(sources.empty()){
                LOG(INFO) << "Did not find any input source for: ";
                for(auto s : sourceUrls){
                    LOG(INFO) << s;
                }
            }

            if(sources.size() > 1){
                LOG(ERROR) << "Too many sources found at URL, unable to continue.";
                return nullptr;
            }

            frameSource = sources.begin()->second.first;
            sourceId = sources.begin()->first;

            const auto sourceInfo = sources.begin()->second.second;
            sourceUrlDisplay = sourceInfo.url;

            if(sourceInfo.tfsContentType == "timing-loop-raw" || sourceInfo.tfsContentType == "pit-loop-raw"){
                auto loopDataRepublihserNode = pipeline.createProcessingNode(
                        std::make_shared<LoopDataRepublisher>(appServices, propNode, std::make_shared<NATSConnection>(brokerURL))
                );

                pipeline.connect(source->getOutput(), loopDataRepublihserNode->getInput());

                LOG(INFO) << "Detected timing loop source, so attempting to republish data to nats on " << brokerURL << " to topic timing-loop-raw-republish";

            }

            mainWindow->addSourceStatus(sourceId, frameSource->getFrameSourceStatus(), !sourceInfo.partOfSyncedMultiFrameSource);
        }

        auto frameCallback = [source, sourceId](const FramePtr& frame) {
            frame->setSourceId(sourceId);
            source->push(frame);
        };

        auto frameGetter = [this](int64_t t){
            ZoneScopedN("frameGetter");
            if(t < 0)
                return pool.take();
            else
                return pool.takeWithTimeout(t);
        };

        frameSource->start(frameCallback, frameGetter);

        return mainWindow;
    }

    std::shared_ptr<WindowGlfwBgfxImgui::WindowRenderer> getWindowRenderer(GUIAppSettings&) override {
        auto mainWindow = initApp();
        if (!mainWindow)
            return nullptr;

        playbackControlUISettings->loopSimulateLive = loopSimulateLive;
        playbackControlUISettings->publisherFollowerCommunicationURL = announceURL.value_or("nats://localhost:4222");
        playbackControlUISettings->defaultDrop = true;
        playbackControlUISettings->bookmarkManager = nullptr;
        playbackControlUISettings->drawBookmarkCallback = nullptr;
        playbackControlUISettings->bookmarkClickedCallback = nullptr;

        if (!syncedMultiFrameSource->getSourceInfos().empty()) {
            std::shared_ptr<PlaybackControlUI> playbackUI = PlaybackControlUIFactory::createPlaybackUI(
                    propNode,
                    frameSource,
                    syncedMultiFrameSource,
                    appServices,
                    [this](int64_t newTimestampAdjustment) { timeAdjustment = newTimestampAdjustment; },
                    playbackControlUISettings.get());

            mainWindow->setFrameBasedPlaybackControl(playbackUI);
        }

        return mainWindow;
    }

    int run() override {
        if (!initApp())
            return -1;

        while(!sigTermReceived()){
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return 0;
    }

    std::vector<std::string> sourceUrls;
    std::string feedName;
    std::vector<std::string> audioStreamNames;
    std::string videoServerFeedType = "video";
    std::string brokerURL = "nats://127.0.0.1:4222";
    std::optional<std::string> announceURL;
    std::string loopTimestampStart;
    std::string loopTimestampEnd;
    bool loopSimulateLive = false;
    // bool knownOneFramePerPacketLowLatency = true;
    std::optional<int> rotationOver90;
    bool record = false;
    int64_t maxNumFramesPerStore = 60*60*5;
    int64_t maxNumStores = 30;

    bool createReEncodedVideoPublisher = false;
    uint16_t reEncodedVideoPublisherPortNum = 4224;

    bool createH26xPublisher = false;
    uint16_t h26xPublisherPortNum = 4223;

    bool createH26xServer = false;
    bool createPipelineH26xServer = false;
    bool createH26xEncoder = false;

    std::optional<std::string> restreamUrl;

    std::vector<int> subsampleImageWidths;
    std::vector<std::string> subsampleScaleFactorsStrings;
    int subsampleSkipRate = 1;

    bool useSEITimestamp = false;
    bool useHevcSEITimestamp = false; //legacy

    std::optional<int> jpegQualityOverride;
    std::optional<double> targetBitsPerPixel;

    std::string configurationJSONPath = "Capture.json";
    int framePoolSize = 16;

    int64_t numFramesPerH26xFile = 60*60*5;
    int64_t numH26xFiles = 12 * 12;

    int remoteStreamBitrate = 1'000'000;

    std::string horusCameraControlUrl;
    std::string imageDumpPrefix = "";

    std::uintmax_t maxStoresBytesSize = 0;

    bool republishAsKlv = false;
    std::string klvRepublishBrokerUrl = "";
    bool useScrubbableH26x = false;
    bool useScrubbableGpuCache = false;
    bool encodeFullSizeJpeg = true;
    bool createDecklinkOutput = false;

    DecklinkOutputOptions decklinkOutputOptions;

    bool publishEncodedH26xOverNats = false;
    bool publishEncodedH26xOverZMQ = false;
    uint16_t encodedH26xZmqPubPort = 4333;
    bool disableDecodingStream = false;

    bool allowFutureFrameTimes = false;
    bool allowBackwardsTimeJumps = false;

    bool combineSources = false;
    bool deinterlaceCombinedSources = true;

    bool forceCpuDecodeAllSources = false;
    bool forceCpuDecodeJpegSources = false;
    bool forceCpuDecodeH26xSources = false;

    bool ccamAdjustTimestampWithExposure = true;

    std::optional<std::string> backupUrl;

    std::string playbackStartTimeString;

    std::unique_ptr<PlaybackControlUIFactory::Settings> playbackControlUISettings;

    PropNode propNode = createPropNode();
    PropVarInt timeAdjustment = {propNode, "time adjustment", 0, IntAttributes(std::nullopt, std::nullopt, 1, "Added to all timestamps. In microseconds.")};

    ConcurrentPool<Frame> pool;

    FramePipeline pipeline;

    std::unique_ptr<Sub> timeSyncAdjustmentSub;

    std::shared_ptr<SyncedMultiFrameSource> syncedMultiFrameSource = std::make_shared<SyncedMultiFrameSource>();
    std::shared_ptr<FrameProcessorApplicationServices> appServices;
    std::shared_ptr<FrameSource> frameSource;
    std::shared_ptr<VancHexViewerProcessor> vancHexViewerProcessor;
};
}

int main(int argc, char *argv[]){
    using namespace apg;
    return guiAppMain(argc, argv, std::make_shared<CaptureApp>());
}