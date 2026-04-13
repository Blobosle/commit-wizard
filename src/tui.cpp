#include <commitwizard.h>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include <cerrno>
#include <csignal>
#include <termios.h>
#include <unistd.h>
#include <iostream>

using namespace ftxui;

static volatile std::sig_atomic_t g_should_exit = 0;

auto render() {
    return vbox({
            window(text("TASK") | bold, vbox(hbox({text("Hello")})))
            });
}

void init_tui() {
    termios oldt{};
    termios raw{};
    struct sigaction sa{};
    struct sigaction old_sa{};

    /* Signal handler for quitting with SIGINT */
    sa.sa_handler = [](int) {
        g_should_exit = 1;
    };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, &old_sa);

    /* Use canon mode for terminal */
    tcgetattr(STDIN_FILENO, &oldt);
    raw = oldt;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    std::string resetp;

    /* \x1b[?1049h enters the terminal alternate screen. */
    /* \x1b[?25l hides the cursor. */
    std::cout << "\x1b[?1049h" << "\x1b[?25l";

    for (;;) {
        if (g_should_exit) {
            break;
        }

        auto document = render();
        auto screen = Screen::Create(Dimension::Full(), Dimension::Full());
        Render(screen, document);

        std::cout << resetp;
        screen.Print();
        resetp = screen.ResetPosition();

        /* TODO: May need to standardize this thing for input shortcuts */
        char input = 0;
        int bytes_read = read(STDIN_FILENO, &input, 1);

        if ((bytes_read == 1 && input == 'q') ||
                (bytes_read == -1 && errno == EINTR && g_should_exit)) {
            break;
        }
    }

    sigaction(SIGINT, &old_sa, nullptr);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

    /* \x1b[?25h shows the cursor. */
    /* \x1b[?1049l leaves the terminal alternate screen. */
    std::cout << "\x1b[?25h" << "\x1b[?1049l";
}
