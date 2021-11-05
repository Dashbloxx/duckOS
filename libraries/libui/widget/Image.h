//
// Created by aaron on 11/4/2021.
//

#ifndef DUCKOS_LIBUI_IMAGE_H
#define DUCKOS_LIBUI_IMAGE_H

#include "Widget.h"

namespace UI {
    class Image: public Widget {
    public:
        enum ScalingMode {
            STRETCH,
            CENTER,
            FILL
        };

        explicit Image(const Gfx::Image& image, ScalingMode mode = CENTER);

        //Image
        const Gfx::Image& image();
        void set_image(const Gfx::Image& image);
        ScalingMode scaling_mode();
        void set_scaling_mode(ScalingMode mode);

        //Widget
        void do_repaint(const UI::DrawContext& ctx) override;
        Dimensions preferred_size() override;

    private:
        Gfx::Image _image;
        ScalingMode _scaling_mode;
    };
}

#endif //DUCKOS_LIBUI_IMAGE_H