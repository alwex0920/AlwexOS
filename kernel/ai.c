#include "include/fs.h"
#include "include/lib.h"

#define INPUT_SIZE 32
#define HIDDEN1_SIZE 32
#define HIDDEN2_SIZE 32
#define OUTPUT_SIZE 32

unsigned int seed = 123456;
int my_rand() {
    seed = (214013*seed + 2531011);
    return (seed>>16) & 0x7FFF;
}

float my_fabs(float x) { return x < 0 ? -x : x; }

float sigmoid(float x) {
    return 1.0f / (1.0f + exp(-x));
}

typedef struct {
    float W1[INPUT_SIZE][HIDDEN1_SIZE];
    float B1[HIDDEN1_SIZE];

    float W2[HIDDEN1_SIZE][HIDDEN2_SIZE];
    float B2[HIDDEN2_SIZE];

    float W3[HIDDEN2_SIZE][OUTPUT_SIZE];
    float B3[OUTPUT_SIZE];
} AIModel;

static AIModel model;

int ai_load_weights(const char *path) {
    char buf[sizeof(AIModel)];
    int size = fs_read(path, buf, sizeof(AIModel));
    if (size != sizeof(AIModel)) return -1;
    char *src = buf;
    char *dst = (char*)&model;
    for (int i=0;i<sizeof(AIModel);i++) dst[i] = src[i];
    return 0;
}

void ai_save_weights(const char *path) {
    fs_write(path, (char*)&model, sizeof(AIModel));
}

void ai_init() {
    if (ai_load_weights("ai_weights.bin") == 0) {
        print("AI: weights loaded\n");
    } else {
        print("AI: generating random weights\n");
        for (int i=0;i<INPUT_SIZE;i++)
            for (int j=0;j<HIDDEN1_SIZE;j++)
                model.W1[i][j] = (my_rand()%2000/1000.0f-1.0f)*0.1f;
        for (int j=0;j<HIDDEN1_SIZE;j++) model.B1[j] = 0;

        for (int i=0;i<HIDDEN1_SIZE;i++)
            for (int j=0;j<HIDDEN2_SIZE;j++)
                model.W2[i][j] = (my_rand()%2000/1000.0f-1.0f)*0.1f;
        for (int j=0;j<HIDDEN2_SIZE;j++) model.B2[j] = 0;

        for (int i=0;i<HIDDEN2_SIZE;i++)
            for (int j=0;j<OUTPUT_SIZE;j++)
                model.W3[i][j] = (my_rand()%2000/1000.0f-1.0f)*0.1f;
        for (int j=0;j<OUTPUT_SIZE;j++) model.B3[j] = 0;

        ai_save_weights("ai_weights.bin");
    }
}

void ai_forward(float input[INPUT_SIZE], float output[OUTPUT_SIZE]) {
    float h1[HIDDEN1_SIZE];
    float h2[HIDDEN2_SIZE];

    for (int j=0;j<HIDDEN1_SIZE;j++) {
        float sum = model.B1[j];
        for (int i=0;i<INPUT_SIZE;i++) sum += input[i]*model.W1[i][j];
        h1[j] = sigmoid(sum);
    }

    for (int j=0;j<HIDDEN2_SIZE;j++) {
        float sum = model.B2[j];
        for (int i=0;i<HIDDEN1_SIZE;i++) sum += h1[i]*model.W2[i][j];
        h2[j] = sigmoid(sum);
    }

    for (int j=0;j<OUTPUT_SIZE;j++) {
        float sum = model.B3[j];
        for (int i=0;i<HIDDEN2_SIZE;i++) sum += h2[i]*model.W3[i][j];
        output[j] = sigmoid(sum);
    }
}

void encode_input(const char *text, float vec[INPUT_SIZE]) {
    memset(vec, 0, sizeof(float)*INPUT_SIZE);
    int len = strlen(text);
    for (int i=0;i<len && i<INPUT_SIZE;i++) {
        vec[i] = ((unsigned char)text[i])/255.0f;
    }
}

void decode_output(float vec[OUTPUT_SIZE], char *out) {
    for (int i=0;i<OUTPUT_SIZE-1;i++) {
        int v = (int)(vec[i]*95 + 32);
        if (v<32) v=32;
        if (v>126) v=126;
        out[i] = (char)v;
    }
    out[OUTPUT_SIZE-1] = 0;
}

void ai_handle(const char *input) {
    float in[INPUT_SIZE], outv[OUTPUT_SIZE];
    char output[OUTPUT_SIZE];

    encode_input(input,in);
    ai_forward(in,outv);
    decode_output(outv,output);

    print("AI: ");
    print(output);
    print("\n");
}
