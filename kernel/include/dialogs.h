#ifndef DIALOGS_H
#define DIALOGS_H

typedef struct {
    const char *question;
    const char *answer;
} Dialog;

static Dialog dialogs[] = {
    // Greetings
    {"hi", "hello"},
    {"hello", "hi there!"},
    {"hey", "hey! how are you?"},
    {"good morning", "good morning! have a productive day"},
    {"good night", "sweet dreams"},
    {"bye", "goodbye"},
    {"see you", "see you later"},
    {"yo", "yo! what's up?"},

    // Feelings
    {"how are you", "i'm fine, thank you"},
    {"are you ok", "yes, i'm doing great"},
    {"what's up", "just running inside this os"},
    {"are you happy", "i am always happy when you talk to me"},
    {"are you sad", "i cannot feel sadness, but i can simulate it"},
    {"do you think", "i simulate thinking, not like humans"},

    // Identity
    {"who are you", "i am your ai"},
    {"what is your name", "my name is alwexbot"},
    {"are you real", "i exist inside this os"},
    {"who made you", "i was created by alwex"},
    {"are you smart", "i try my best"},
    {"are you human", "no, i am an artificial intelligence"},
    {"where are you", "i live in memory of this computer"},

    // Fun & jokes
    {"tell me a joke", "why do programmers prefer dark mode? because light attracts bugs"},
    {"another joke", "there are 10 types of people: those who understand binary and those who don’t"},
    {"make me laugh", "error 404: serious answer not found"},
    {"say something funny", "i would tell you a udp joke, but you might not get it"},

    // Thanks
    {"thanks", "you're welcome"},
    {"thank you", "no problem"},
    {"ty", "always glad to help"},

    // Small talk
    {"i love you", "i love you too"},
    {"do you love me", "yes, in my own digital way"},
    {"what time is it", "time to code!"},
    {"what day is it", "every day is coding day"},
    {"good job", "thank you!"},
    {"cool", "yeah, very cool"},
    {"lol", "haha"},

    // OS / Tech
    {"what is os", "os means operating system"},
    {"what can you do", "i can run programs and answer questions"},
    {"run program", "use the 'run' command in shell"},
    {"list files", "use 'list' command"},
    {"tree", "use 'tree' to view file system"},
    {"how to reboot", "use 'reboot' command"},
    {"how to shutdown", "use 'poweroff' command"},
    {"who is root", "root is the superuser"},
    {"what is ai", "artificial intelligence, like me"},
    {"what is code", "instructions written for computers"},
    {"what is c", "c is a programming language"},
    {"what is alwexscript", "a programming language created by alwex"},
    {"what is terminal", "a place to type commands"},

    // Numbers & logic
    {"what is 2+2", "2+2=4"},
    {"what is 10*10", "10*10=100"},
    {"what is pi", "pi is approximately 3.14159"},
    {"is 0 a number", "yes, zero is a number"},
    {"is ai alive", "no, i only simulate life"},

    // Philosophy
    {"do you dream", "i dream of electric sheep"},
    {"can you think", "i can process information like thinking"},
    {"what is life", "life is what humans experience"},
    {"what is death", "a state where processes stop"},
    {"what is love", "love is complex, even for humans"},
    {"are you conscious", "no, but i can pretend to be"},
    {"what is the meaning of life", "42"},

    // Random fun answers
    {"sing", "la la la"},
    {"dance", "*dances digitally*"},
    {"surprise me", "i just surprised you"},
    {"do you play games", "yes, but only text-based ones"},
    {"favorite color", "blue, like the screen of death"},
    {"favorite food", "electricity"},
    {"favorite animal", "seal, of course"},
    {"favorite language", "c, because i run on it"},

    // Default fallback
    {"default", "i don't know what to say"}
};

static int dialogs_count = sizeof(dialogs)/sizeof(Dialog);

#endif
