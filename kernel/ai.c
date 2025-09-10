#include "include/fs.h"
#include "include/lib.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define INPUT_SIZE   16
#define HIDDEN_SIZE  8
#define OUTPUT_SIZE  8

float W1[INPUT_SIZE][HIDDEN_SIZE];
float b1[HIDDEN_SIZE];
float W2[HIDDEN_SIZE][OUTPUT_SIZE];
float b2[OUTPUT_SIZE];

char responses[OUTPUT_SIZE][64];
int response_count = 0;

float tanhf_custom(float x) {
    if (x > 10) return 1;
    if (x < -10) return -1;
    return (expf(x) - expf(-x)) / (expf(x) + expf(-x));
}

void softmax(float* z, int n) {
    float max = z[0];
    for (int i = 1; i < n; i++) if (z[i] > max) max = z[i];
    float sum = 0;
    for (int i = 0; i < n; i++) {
        z[i] = expf(z[i] - max);
        sum += z[i];
    }
    for (int i = 0; i < n; i++) z[i] /= sum;
}

int ai_predict(float *input) {
    float hidden[HIDDEN_SIZE];
    float output[OUTPUT_SIZE];

    for (int j = 0; j < HIDDEN_SIZE; j++) {
        float sum = b1[j];
        for (int i = 0; i < INPUT_SIZE; i++)
            sum += input[i] * W1[i][j];
        hidden[j] = tanhf_custom(sum);
    }

    for (int k = 0; k < OUTPUT_SIZE; k++) {
        float sum = b2[k];
        for (int j = 0; j < HIDDEN_SIZE; j++)
            sum += hidden[j] * W2[j][k];
        output[k] = sum;
    }

    softmax(output, OUTPUT_SIZE);

    int best = 0;
    float best_val = output[0];
    for (int k = 1; k < OUTPUT_SIZE; k++) {
        if (output[k] > best_val) {
            best_val = output[k];
            best = k;
        }
    }
    return best;
}

void text_to_vec(const char* text, float* input) {
    for (int i = 0; i < INPUT_SIZE; i++) input[i] = 0;
    for (int i = 0; text[i] && i < INPUT_SIZE; i++) {
        input[i] = (float)(text[i] % 16) / 16.0f;
    }
}

void ai_handle(const char* text) {
    float input[INPUT_SIZE];
    text_to_vec(text, input);

    if (response_count == 0) {
        print("AI: No responses loaded.\n");
        return;
    }

    int idx = ai_predict(input);
    if (idx < response_count) {
        print("AI: ");
        print(responses[idx]);
        print("\n");
    } else {
        print("AI: I don't know.\n");
    }
}

void ai_save_weights() {
    int size = sizeof(W1) + sizeof(b1) + sizeof(W2) + sizeof(b2);
    char* buffer = malloc(size);
    if (!buffer) return;
    char* p = buffer;
    memcpy(p, W1, sizeof(W1)); p += sizeof(W1);
    memcpy(p, b1, sizeof(b1)); p += sizeof(b1);
    memcpy(p, W2, sizeof(W2)); p += sizeof(W2);
    memcpy(p, b2, sizeof(b2));

    fs_write("/ai_weights.dat", buffer, size);
    free(buffer);
    print("AI: weights saved.\n");
}

void ai_load_weights() {
    int size = sizeof(W1) + sizeof(b1) + sizeof(W2) + sizeof(b2);
    char* buffer = malloc(size);
    if (!buffer) return;
    int read = fs_read("/ai_weights.dat", buffer, size);
    if (read != size) {
        print("AI: no saved weights, using defaults.\n");
        free(buffer);
        return;
    }
    char* p = buffer;
    memcpy(W1, p, sizeof(W1)); p += sizeof(W1);
    memcpy(b1, p, sizeof(b1)); p += sizeof(b1);
    memcpy(W2, p, sizeof(W2)); p += sizeof(W2);
    memcpy(b2, p, sizeof(b2));
    free(buffer);
    print("AI: weights loaded.\n");
}
