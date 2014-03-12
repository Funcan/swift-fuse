#include <stdio.h>
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>

struct url_data {
    size_t size;
    char *data;
};

size_t
write_data(void *ptr, size_t size, size_t nmemb, struct url_data *data) {
    size_t index = data->size;
    size_t n = (size * nmemb);
    char *tmp;

    data->size += (size * nmemb);

#ifdef DEBUG
    fprintf(stderr, "data at %p size=%ld nmemb=%ld\n", ptr, size, nmemb);
#endif
    tmp = realloc(data->data, data->size + 1);    /* +1 for '\0' */

    if (tmp) {
        data->data = tmp;
        } else {
        if (data->data) {
            free(data->data);
        }

        fprintf(stderr, "Failed to allocate memory.\n");
        return 0;
    }

    memcpy((data->data + index), ptr, n);
    data->data[data->size] = '\0';

    return size * nmemb;
}

static void
dump(const char *text, FILE * stream, unsigned char *ptr, size_t size, int nohex) {
    size_t i;
    size_t c;

    unsigned int width = 0x10;

    if (nohex) {
        /* without the hex output, we can fit more on screen */
        width = 0x40;
    }

    fprintf(stream, "%s, %10.10ld bytes (0x%8.8lx)\n",
                text, (long) size, (long) size);

    for (i = 0; i < size; i += width) {
        fprintf(stream, "%4.4lx: ", (long) i);

        if (!nohex) {
            /* hex not disabled, show it */
            for (c = 0; c < width; c++) {
                if (i + c < size) {
                    fprintf(stream, "%02x ", ptr[i + c]);
                } else {
                    fputs("   ", stream);
                }
            }
        }

        for (c = 0; (c < width) && (i + c < size); c++) {
            /* check for 0D0A; if found, skip past and start a new line of output */
            if (nohex && (i + c + 1 < size) && ptr[i + c] == 0x0D
                && ptr[i + c + 1] == 0x0A) {
                i += (c + 2 - width);
                break;
            }
            fprintf(stream, "%c", (ptr[i + c] >= 0x20)
                    && (ptr[i + c] < 0x80) ? ptr[i + c] : '.');
            /* check again for 0D0A, to avoid an extra \n if it's at width */
            if (nohex && (i + c + 2 < size) && ptr[i + c + 1] == 0x0D
                && ptr[i + c + 2] == 0x0A) {
                i += (c + 3 - width);
                break;
            }
        }
        fputc('\n', stream);    /* newline */
    }
    fflush(stream);
}

static int
my_trace(CURL * handle, curl_infotype type,
      char *data, size_t size, void *userp) {
    const char *text;
    (void) handle;        /* prevent compiler warning */

    switch (type) {
        case CURLINFO_TEXT:
            fprintf(stderr, "== Info: %s", data);

        case CURLINFO_HEADER_OUT:
            text = "=> Send header";
            break;
        case CURLINFO_DATA_OUT:
            text = "=> Send data";
            break;
        case CURLINFO_SSL_DATA_OUT:
            text = "=> Send SSL data";
            break;
        case CURLINFO_HEADER_IN:
            text = "<= Recv header";
            break;
        case CURLINFO_DATA_IN:
            text = "<= Recv data";
            break;
        case CURLINFO_SSL_DATA_IN:
            text = "<= Recv SSL data";
            break;
        default:
            return 0;
    }

    dump(text, stderr, (unsigned char *) data, size, 1);
    return 0;
}


char *
handle_post(char *url, char *postdata) {
    CURL *curl;

    struct url_data data;
    data.size = 0;
    data.data = malloc(4096);    /* reasonable size initial buffer */
    if (NULL == data.data) {
        fprintf(stderr, "Failed to allocate memory.\n");
        return NULL;
    }

    data.data[0] = '\0';

    CURLcode res;

    curl = curl_easy_init();
    if (curl) {
        struct curl_slist *chunk = NULL;

        chunk = curl_slist_append(chunk, "Accept: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "swift-fuse/0.1");
        curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, my_trace);
        curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata);
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr,
                    "curl_easy_perform() failed: %s\n",
                    curl_easy_strerror(res));
            exit(1);
        }

        curl_easy_cleanup(curl);
    }
    return data.data;
}

char *
do_login(char *url, char *username, char *password, char *tenant) {
    char *postdata = malloc(4096);
    char *fullurl = malloc(4096);
    if ((NULL == postdata) || (NULL == fullurl)) {
        perror("malloc");
        exit(1);
    }

    snprintf(postdata, 4096,
          "{\"auth\": {\"tenantName\": \"%s\", \"passwordCredentials\": {\"username\": \"%s\", \"password\": \"%s\"}}}",
          tenant, username, password);
    snprintf(fullurl, 4096, "%s/tokens", url);

    handle_post(fullurl, postdata);
}

int
main(int argc, char *argv[]) {
    char *auth_url = getenv("OS_AUTH_URL");
    char *region = getenv("OS_REGION_NAME");
    char *password = getenv("OS_PASSWORD");
    char *username = getenv("OS_USERNAME");
    char *tenant = getenv("OS_TENANT_NAME");
    char *data;
    if ((NULL == auth_url) || (NULL == region) || (NULL == password) ||
        (NULL == username) || (NULL == tenant)) {
        fprintf(stderr,
                "Need to provide OS_AUTH_URL OS_REGION_NAME OS_PASSWORD "
                "OS_USERNAME OS_TENANT_NAME\n");
        exit(1);
    }

    data = do_login(auth_url, username, password, tenant);
    if (data) {
        printf("%s\n", data);
        free(data);
    }

    return 0;
}
