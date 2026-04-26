#include "control_protocol.hpp"

#include <gtk/gtk.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace {

constexpr const char *kSocketPath = "/run/steamdeckcontroller/control.sock";

GtkWidget *g_status_label = nullptr;
GtkWidget *g_detail_label = nullptr;
GtkWidget *g_start_button = nullptr;
GtkWidget *g_stop_button = nullptr;

std::string send_command(const std::string &command) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return std::string("ERR socket failed: ") + std::strerror(errno);
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, kSocketPath, sizeof(address.sun_path) - 1);

    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        const std::string error = std::strerror(errno);
        close(fd);
        return "ERR Cannot connect to daemon at " + std::string(kSocketPath) + ": " + error;
    }

    std::string wire = sdc::format_control_command(command);
    if (write(fd, wire.data(), wire.size()) < 0) {
        const std::string error = std::strerror(errno);
        close(fd);
        return "ERR write failed: " + error;
    }

    std::string response;
    char buffer[512]{};
    for (;;) {
        const ssize_t read_count = read(fd, buffer, sizeof(buffer));
        if (read_count <= 0) {
            break;
        }
        response.append(buffer, static_cast<size_t>(read_count));
    }
    close(fd);

    while (!response.empty() && (response.back() == '\n' || response.back() == '\r')) {
        response.pop_back();
    }
    if (response.empty()) {
        return "ERR daemon returned no response";
    }
    return response;
}

void update_from_response(const std::string &response) {
    const sdc::FrontendResponse parsed = sdc::parse_frontend_response(response);

    gtk_label_set_text(GTK_LABEL(g_status_label), parsed.headline.c_str());
    gtk_label_set_text(GTK_LABEL(g_detail_label), parsed.details.c_str());

    if (parsed.state == sdc::CaptureState::Error || parsed.state == sdc::CaptureState::Stopped) {
        gtk_widget_set_sensitive(g_start_button, TRUE);
        gtk_widget_set_sensitive(g_stop_button, FALSE);
    } else if (parsed.state == sdc::CaptureState::Running) {
        gtk_widget_set_sensitive(g_start_button, FALSE);
        gtk_widget_set_sensitive(g_stop_button, TRUE);
    }
}

gboolean refresh_status(gpointer) {
    update_from_response(send_command("STATUS"));
    return G_SOURCE_CONTINUE;
}

void on_start_clicked(GtkButton *, gpointer) {
    update_from_response(send_command("START"));
    refresh_status(nullptr);
}

void on_stop_clicked(GtkButton *, gpointer) {
    update_from_response(send_command("STOP"));
    refresh_status(nullptr);
}

void on_destroy(GtkWidget *, gpointer) {
    gtk_main_quit();
}

} // namespace

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Steam Deck Controller Passthrough");
    gtk_window_set_default_size(GTK_WINDOW(window), 560, 240);
    gtk_container_set_border_width(GTK_CONTAINER(window), 18);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), nullptr);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(window), box);

    GtkWidget *title = gtk_label_new("USB input passthrough");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(attrs, pango_attr_scale_new(1.4));
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);

    g_status_label = gtk_label_new("Disconnected");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), g_status_label, FALSE, FALSE, 0);

    g_detail_label = gtk_label_new("Daemon status is not available yet.");
    gtk_widget_set_halign(g_detail_label, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(g_detail_label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(g_detail_label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), g_detail_label, TRUE, TRUE, 0);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(box), buttons, FALSE, FALSE, 0);

    g_start_button = gtk_button_new_with_label("Start");
    g_stop_button = gtk_button_new_with_label("Stop");
    gtk_widget_set_sensitive(g_stop_button, FALSE);
    g_signal_connect(g_start_button, "clicked", G_CALLBACK(on_start_clicked), nullptr);
    g_signal_connect(g_stop_button, "clicked", G_CALLBACK(on_stop_clicked), nullptr);
    gtk_box_pack_start(GTK_BOX(buttons), g_start_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), g_stop_button, FALSE, FALSE, 0);

    gtk_widget_show_all(window);
    refresh_status(nullptr);
    g_timeout_add_seconds(1, refresh_status, nullptr);
    gtk_main();
    return 0;
}
