#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "action/action.h"
#include "task/uitask.h"
#include "../core/core.h"
#include "../libs/quirc/quirc_internal.h"

static bool remoteinstall_get_last_urls(char* out, size_t size) {
    if(out == NULL || size == 0) {
        return false;
    }

    Handle file = 0;
    if(R_FAILED(FSUSER_OpenFileDirectly(&file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, "/fbi/lasturls"), FS_OPEN_READ, 0))) {
        return false;
    }

    u32 bytesRead = 0;
    FSFILE_Read(file, &bytesRead, 0, out, size - 1);
    out[bytesRead] = '\0';

    FSFILE_Close(file);

    return bytesRead != 0;
}

static Result remoteinstall_set_last_urls(const char* urls) {
    Result res = 0;

    FS_Archive sdmcArchive = 0;
    if(R_SUCCEEDED(res = FSUSER_OpenArchive(&sdmcArchive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
        FS_Path path = fsMakePath(PATH_ASCII, "/fbi/lasturls");

        Handle file = 0;
        if(R_SUCCEEDED(FSUSER_OpenFile(&file, sdmcArchive, path, FS_OPEN_READ, 0))) {
            FSFILE_Close(file);

            res = FSUSER_DeleteFile(sdmcArchive, path);
        }

        if(urls != NULL && strlen(urls) != 0
           && R_SUCCEEDED(res)
           && R_SUCCEEDED(res = fs_ensure_dir(sdmcArchive, "/fbi/"))
           && R_SUCCEEDED(res = FSUSER_OpenFile(&file, sdmcArchive, path, FS_OPEN_WRITE | FS_OPEN_CREATE, 0))) {
            u32 bytesWritten = 0;
            res = FSFILE_Write(file, &bytesWritten, 0, urls, strlen(urls), FS_WRITE_FLUSH | FS_WRITE_UPDATE_TIME);

            Result closeRes = FSFILE_Close(file);
            if(R_SUCCEEDED(res)) {
                res = closeRes;
            }
        }

        Result closeRes = FSUSER_CloseArchive(sdmcArchive);
        if(R_SUCCEEDED(res)) {
            res = closeRes;
        }
    }

    return res;
}

typedef struct {
    int serverSocket;
    int clientSocket;
} remoteinstall_network_data;

static int remoteinstall_network_recvwait(int sockfd, void* buf, size_t len, int flags) {
    errno = 0;

    int ret = 0;
    size_t read = 0;
    while((((ret = recv(sockfd, buf + read, len - read, flags)) > 0 && (read += ret) < len) || errno == EAGAIN) && !(hidKeysDown() & KEY_B)) {
        errno = 0;
    }

    return ret < 0 ? ret : (int) read;
}

static int remoteinstall_network_sendwait(int sockfd, void* buf, size_t len, int flags) {
    errno = 0;

    int ret = 0;
    size_t written = 0;
    while((((ret = send(sockfd, buf + written, len - written, flags)) > 0 && (written += ret) < len) || errno == EAGAIN) && !(hidKeysDown() & KEY_B)) {
        errno = 0;
    }

    return ret < 0 ? ret : (int) written;
}

static void remoteinstall_network_close_client(void* data) {
    remoteinstall_network_data* networkData = (remoteinstall_network_data*) data;

    if(networkData->clientSocket != 0) {
        u8 ack = 0;
        remoteinstall_network_sendwait(networkData->clientSocket, &ack, sizeof(ack), 0);

        close(networkData->clientSocket);
        networkData->clientSocket = 0;
    }
}


static void remoteinstall_network_free_data(remoteinstall_network_data* data) {
    remoteinstall_network_close_client(data);

    if(data->serverSocket != 0) {
        close(data->serverSocket);
        data->serverSocket = 0;
    }

    free(data);
}

static void remoteinstall_network_update(ui_view* view, void* data, float* progress, char* text) {
    remoteinstall_network_data* networkData = (remoteinstall_network_data*) data;

    if(hidKeysDown() & KEY_B) {
        ui_pop();
        info_destroy(view);

        remoteinstall_network_free_data(networkData);

        return;
    }

    struct sockaddr_in client;
    socklen_t clientLen = sizeof(client);

    int sock = accept(networkData->serverSocket, (struct sockaddr*) &client, &clientLen);
    if(sock >= 0) {
        networkData->clientSocket = sock;

        u32 size = 0;
        if(remoteinstall_network_recvwait(networkData->clientSocket, &size, sizeof(size), 0) != sizeof(size)) {
            error_display_errno(NULL, NULL, errno, "페이로드 길이(payload length)를 읽는 데 실패했습니다.");

            remoteinstall_network_close_client(data);
            return;
        }

        size = ntohl(size);
        if(size >= DOWNLOAD_URL_MAX * INSTALL_URLS_MAX) {
            error_display(NULL, NULL, "페이로드가 너무 큽니다.");

            remoteinstall_network_close_client(data);
            return;
        }

        char* urls = (char*) calloc(size + 1, sizeof(char));
        if(urls == NULL) {
            error_display(NULL, NULL, "URL 버퍼를 할당하는 데 실패했습니다..");

            remoteinstall_network_close_client(data);
            return;
        }

        if(remoteinstall_network_recvwait(networkData->clientSocket, urls, size, 0) != size) {
            error_display_errno(NULL, NULL, errno, "URL를 읽는 데 실패했습니다.");

            free(urls);
            remoteinstall_network_close_client(data);
            return;
        }

        remoteinstall_set_last_urls(urls);
        action_install_url("수신받은 URL로 설치할까요?", urls, NULL, data, NULL, remoteinstall_network_close_client, NULL);

        free(urls);
    } else if(errno != EAGAIN) {
        if(errno == 22 || errno == 115) {
            ui_pop();
            info_destroy(view);
        }

        error_display_errno(NULL, NULL, errno, "소켓을 여는 데 실패했습니다.");

        if(errno == 22 || errno == 115) {
            remoteinstall_network_free_data(networkData);

            return;
        }
    }

    struct in_addr addr = {(in_addr_t) gethostid()};
    snprintf(text, PROGRESS_TEXT_MAX, "연결 대기 중...\nIP: %s\n포트: 5000", inet_ntoa(addr));
}

static void remoteinstall_receive_urls_network() {
    remoteinstall_network_data* data = (remoteinstall_network_data*) calloc(1, sizeof(remoteinstall_network_data));
    if(data == NULL) {
        error_display(NULL, NULL, "네트워크 설치 데이터를 할당하는 데 실패했습니다.");

        return;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if(sock < 0) {
        error_display_errno(NULL, NULL, errno, "서버 소켓을 여는 데 실패했습니다.");

        remoteinstall_network_free_data(data);
        return;
    }

    data->serverSocket = sock;

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(5000);
    server.sin_addr.s_addr = (in_addr_t) gethostid();

    if(bind(data->serverSocket, (struct sockaddr*) &server, sizeof(server)) < 0) {
        error_display_errno(NULL, NULL, errno, "서버 소켓을 바인딩하는데 실패했습니다.");

        remoteinstall_network_free_data(data);
        return;
    }

    fcntl(data->serverSocket, F_SETFL, fcntl(data->serverSocket, F_GETFL, 0) | O_NONBLOCK);

    if(listen(data->serverSocket, 5) < 0) {
        error_display_errno(NULL, NULL, errno, "서버 소켓을 수신하지 못했습니다.");

        remoteinstall_network_free_data(data);
        return;
    }

    info_display("URL 수신", "B: 뒤로", false, data, remoteinstall_network_update, NULL);
}

#define QR_IMAGE_WIDTH 400
#define QR_IMAGE_HEIGHT 240

typedef struct {
    struct quirc* qrContext;
    u32 tex;

    bool capturing;
    capture_cam_data captureInfo;
} remoteinstall_qr_data;

static void remoteinstall_qr_stop_capture(remoteinstall_qr_data* data) {
    if(!data->captureInfo.finished) {
        svcSignalEvent(data->captureInfo.cancelEvent);
        while(!data->captureInfo.finished) {
            svcSleepThread(1000000);
        }
    }

    data->capturing = false;

    if(data->captureInfo.buffer != NULL) {
        memset(data->captureInfo.buffer, 0, QR_IMAGE_WIDTH * QR_IMAGE_HEIGHT * sizeof(u16));
    }
}

static void remoteinstall_qr_free_data(remoteinstall_qr_data* data) {
    remoteinstall_qr_stop_capture(data);

    if(data->captureInfo.buffer != NULL) {
        free(data->captureInfo.buffer);
        data->captureInfo.buffer = NULL;
    }

    if(data->tex != 0) {
        screen_unload_texture(data->tex);
        data->tex = 0;
    }

    if(data->qrContext != NULL) {
        quirc_destroy(data->qrContext);
        data->qrContext = NULL;
    }

    free(data);
}

static void remoteinstall_qr_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    remoteinstall_qr_data* installData = (remoteinstall_qr_data*) data;

    if(installData->tex != 0) {
        svcWaitSynchronization(installData->captureInfo.mutex, U64_MAX);
        screen_load_texture_untiled(installData->tex, installData->captureInfo.buffer, QR_IMAGE_WIDTH * QR_IMAGE_HEIGHT * sizeof(u16), QR_IMAGE_WIDTH, QR_IMAGE_HEIGHT, GPU_RGB565, false);
        svcReleaseMutex(installData->captureInfo.mutex);

        screen_draw_texture(installData->tex, 0, 0, QR_IMAGE_WIDTH, QR_IMAGE_HEIGHT);
    }
}

static void remoteinstall_qr_update(ui_view* view, void* data, float* progress, char* text) {
    remoteinstall_qr_data* installData = (remoteinstall_qr_data*) data;

    if(hidKeysDown() & KEY_B) {
        ui_pop();
        info_destroy(view);

        remoteinstall_qr_free_data(installData);

        return;
    }

    if(hidKeysDown() & KEY_X) {
        remoteinstall_qr_stop_capture(installData);

        installData->captureInfo.camera = installData->captureInfo.camera == CAMERA_OUTER ? CAMERA_INNER : CAMERA_OUTER;

        return;
    }

    if(!installData->capturing) {
        Result capRes = task_capture_cam(&installData->captureInfo);
        if(R_FAILED(capRes)) {
            ui_pop();
            info_destroy(view);

            error_display_res(NULL, NULL, capRes, "카메라 캡쳐를 시작하지 못했습니다.");

            remoteinstall_qr_free_data(installData);
            return;
        } else {
            installData->capturing = true;
        }
    }

    if(installData->captureInfo.finished) {
        ui_pop();
        info_destroy(view);

        if(R_FAILED(installData->captureInfo.result)) {
            error_display_res(NULL, NULL, installData->captureInfo.result, "카메라 프레임을 캡쳐하는 도중 오류가 발생했습니다.");
        }

        remoteinstall_qr_free_data(installData);

        return;
    }

    int w = 0;
    int h = 0;
    uint8_t* qrBuf = quirc_begin(installData->qrContext, &w, &h);

    svcWaitSynchronization(installData->captureInfo.mutex, U64_MAX);

    for(int x = 0; x < w; x++) {
        for(int y = 0; y < h; y++) {
            u16 px = installData->captureInfo.buffer[y * QR_IMAGE_WIDTH + x];
            qrBuf[y * w + x] = (u8) (((((px >> 11) & 0x1F) << 3) + (((px >> 5) & 0x3F) << 2) + ((px & 0x1F) << 3)) / 3);
        }
    }

    svcReleaseMutex(installData->captureInfo.mutex);

    quirc_end(installData->qrContext);

    int qrCount = quirc_count(installData->qrContext);
    for(int i = 0; i < qrCount; i++) {
        struct quirc_code qrCode;
        quirc_extract(installData->qrContext, i, &qrCode);

        struct quirc_data qrData;
        quirc_decode_error_t err = quirc_decode(&qrCode, &qrData);

        if(err == 0) {
            remoteinstall_qr_stop_capture(installData);

            remoteinstall_set_last_urls((const char*) qrData.payload);

            action_install_url("스캔한 QR 코드로 설치할까요?", (const char*) qrData.payload, NULL, NULL, NULL, NULL, NULL);
            return;
        }
    }

    snprintf(text, PROGRESS_TEXT_MAX, "QR 코드 대기 중...");
}

static void remoteinstall_scan_qr_code() {
    remoteinstall_qr_data* data = (remoteinstall_qr_data*) calloc(1, sizeof(remoteinstall_qr_data));
    if(data == NULL) {
        error_display(NULL, NULL, "QR 설치 데이터를 할당하는 데 실패했습니다.");

        return;
    }

    data->tex = 0;

    data->capturing = false;

    data->captureInfo.width = QR_IMAGE_WIDTH;
    data->captureInfo.height = QR_IMAGE_HEIGHT;

    data->captureInfo.camera = CAMERA_OUTER;

    data->captureInfo.finished = true;

    data->qrContext = quirc_new();
    if(data->qrContext == NULL) {
        error_display(NULL, NULL, "QR 컨텍스트를 생성하는 데 실패했습니다.");

        remoteinstall_qr_free_data(data);
        return;
    }

    if(quirc_resize(data->qrContext, QR_IMAGE_WIDTH, QR_IMAGE_HEIGHT) != 0) {
        error_display(NULL, NULL, "QR 컨텍스트 크기 조정에 실패했습니다.");

        remoteinstall_qr_free_data(data);
        return;
    }

    data->captureInfo.buffer = (u16*) calloc(1, QR_IMAGE_WIDTH * QR_IMAGE_HEIGHT * sizeof(u16));
    if(data->captureInfo.buffer == NULL) {
        error_display(NULL, NULL, "이미지 버퍼를 생성할 수 없습니다.");

        remoteinstall_qr_free_data(data);
        return;
    }

    data->tex = screen_allocate_free_texture();

    info_display("QR 코드 설치", "B: 뒤로, X: 카메라 변경", false, data, remoteinstall_qr_update, remoteinstall_qr_draw_top);
}

static void remoteinstall_manually_enter_urls_onresponse(ui_view* view, void* data, SwkbdButton button, const char* response) {
    if(button == SWKBD_BUTTON_CONFIRM) {
        remoteinstall_set_last_urls(response);

        action_install_url("입력한 URL로 설치할까요?", response, NULL, NULL, NULL, NULL, NULL);
    }
}

static void remoteinstall_manually_enter_urls() {
    kbd_display("URL을 입력하세요", "", SWKBD_TYPE_NORMAL, SWKBD_MULTILINE, SWKBD_NOTEMPTY_NOTBLANK, DOWNLOAD_URL_MAX * INSTALL_URLS_MAX, NULL, remoteinstall_manually_enter_urls_onresponse);
}

static void remoteinstall_repeat_last_request() {
    char* textBuf = (char*) calloc(1, DOWNLOAD_URL_MAX * INSTALL_URLS_MAX);
    if(textBuf != NULL) {
        if(remoteinstall_get_last_urls(textBuf, DOWNLOAD_URL_MAX * INSTALL_URLS_MAX)) {
            action_install_url("마지막으로 요청한 URL에서 설치할까요?", textBuf, NULL, NULL, NULL, NULL, NULL);
        } else {
            prompt_display_notify("실패", "최근에 요청한 URL을 찾는 데 실패했습니다.", COLOR_TEXT, NULL, NULL, NULL);
        }

        free(textBuf);
    } else {
        error_display_res(NULL, NULL, R_APP_OUT_OF_MEMORY, "URL 텍스트 버퍼를 할당하는 데 실패했습니다.");
    }
}

static void remoteinstall_forget_last_request() {
    Result forgetRes = remoteinstall_set_last_urls(NULL);
    if(R_SUCCEEDED(forgetRes)) {
        prompt_display_notify("성공", "마지막으로 요청한 URL을 지웠습니다.", COLOR_TEXT, NULL, NULL, NULL);
    } else {
        error_display_res(NULL, NULL, forgetRes, "마지막으로 요청한 URL을 삭제하는 데 실패했습니다.");
    }
}

static list_item receive_urls_network = {"네트워크를 통해 URL 수신", COLOR_TEXT, remoteinstall_receive_urls_network};
static list_item scan_qr_code = {"QR코드 스캔", COLOR_TEXT, remoteinstall_scan_qr_code};
static list_item manually_enter_urls = {"URL 수동 입력", COLOR_TEXT, remoteinstall_manually_enter_urls};
static list_item repeat_last_request = {"마지막 요청 반복", COLOR_TEXT, remoteinstall_repeat_last_request};
static list_item forget_last_request = {"마지막 요청 삭제", COLOR_TEXT, remoteinstall_forget_last_request};

static void remoteinstall_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    if(hidKeysDown() & KEY_B) {
        ui_pop();
        list_destroy(view);

        return;
    }

    if(selected != NULL && (selectedTouched || hidKeysDown() & KEY_A) && selected->data != NULL) {
        ((void(*)()) selected->data)();
        return;
    }

    if(linked_list_size(items) == 0) {
        linked_list_add(items, &receive_urls_network);
        linked_list_add(items, &scan_qr_code);
        linked_list_add(items, &manually_enter_urls);
        linked_list_add(items, &repeat_last_request);
        linked_list_add(items, &forget_last_request);
    }
}

void remoteinstall_open() {
    list_display("원격 설치", "A: 선택, B: 뒤로", NULL, remoteinstall_update, NULL);
}
