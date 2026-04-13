#include <commitwizard.h>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include <array>
#include <cerrno>
#include <csignal>
#include <string_view>
#include <termios.h>
#include <unistd.h>
#include <iostream>

using namespace ftxui;

static Element render_menu(int);
static Element render_git_entries_tab();
static Element render_database_tab();
static Element render_settings_tab();
static int tui_input(int&);

static volatile std::sig_atomic_t g_should_exit = 0;

static int tui_input(int& menu_index) {
    char input = 0;
    int bytes_read = read(STDIN_FILENO, &input, 1);

    if ((bytes_read == 1 && input == 'q') ||
            (bytes_read == -1 && errno == EINTR && g_should_exit)) {
        return 1;
    }

    if (bytes_read != 1) {
        return 0;
    }

    if (input == 'h') {
        menu_index = (menu_index + 2) % 3;
        return 0;
    }

    if (input == 'l') {
        menu_index = (menu_index + 1) % 3;
        return 0;
    }

    if (input == '\x1b') {
        char sequence[2] = {0, 0};
        if (read(STDIN_FILENO, &sequence[0], 1) == 1 &&
                read(STDIN_FILENO, &sequence[1], 1) == 1 &&
                sequence[0] == '[') {
            if (sequence[1] == 'D') {
                menu_index = (menu_index + 2) % 3;
            }
            else if (sequence[1] == 'C') {
                menu_index = (menu_index + 1) % 3;
            }
        }
    }

    return 0;
}

static Element render_menu(int menu_index) {
    std::array<std::string_view, 3> menu_entries = {
        "Git repositories",
        "Database",
        "Settings",
    };

    int num_menu = menu_entries.size();

    Elements tabs;
    tabs.reserve(num_menu);

    for (int i = 0; i < num_menu; i++) {
        auto tab = text(std::string(menu_entries[i]));

        if (static_cast<int>(i) == menu_index) {
            tab = tab | color(Color::SkyBlue1) | bold | inverted;
        }
        else {
            tab = tab | color(Color::SkyBlue1) | dim;
        }

        tabs.push_back(tab);
        if (i + 1 != menu_entries.size()) {
            tabs.push_back(separatorEmpty());
        }
    }

    Element content;

    switch (menu_index) {
        case 0:
            content = render_git_entries_tab();
            break;
        case 1:
            content = render_database_tab();
            break;
        default:
            content = render_settings_tab();
            break;
    }

    return vbox({
            hbox({
                    hbox(std::move(tabs)) | flex,
                    }),
            separator(),
            content
            });
}

static Element render_git_entries_tab() {
    return hbox({
            vbox({
                    window(text("Entries") | bold,
                           filler()) | size(WIDTH, GREATER_THAN, 20) | size(WIDTH, LESS_THAN, 100) | xflex_shrink | yflex,
                    }),
            window(text("Visualization") | bold,
                   filler()) | flex,
            window(text("Commits") | bold,
                   filler()) | flex,
            }) | flex;
}

static Element render_database_tab() {
    return vbox({
            text("Database placeholder"),
            });
}

static Element render_settings_tab() {
    return vbox({
            text("Settings placeholder"),
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
    int menu_index = 0;

    /* \x1b[?1049h enters the terminal alternate screen. */
    /* \x1b[?25l hides the cursor. */
    std::cout << "\x1b[?1049h" << "\x1b[?25l";

    for (;;) {
        if (g_should_exit) {
            break;
        }

        auto document = render_menu(menu_index);
        auto screen = Screen::Create(Dimension::Full(), Dimension::Full());
        Render(screen, document);

        std::cout << resetp;
        screen.Print();
        resetp = screen.ResetPosition();

        if (tui_input(menu_index)) {
            break;
        }
    }

    sigaction(SIGINT, &old_sa, nullptr);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

    /* \x1b[?25h shows the cursor. */
    /* \x1b[?1049l leaves the terminal alternate screen. */
    std::cout << "\x1b[?25h" << "\x1b[?1049l";
}
