// Copyright 2025 NXP
// SPDX-License-Identifier: BSD-3-Clause


#include <RouteMediactl.h>

#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

#include <fcntl.h>
#include <linux/media.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <sys/ioctl.h>

#include <android-base/logging.h>

struct MediaCfg {
    // Datatypes
    struct Format {
        short pad;
        short stream;
        uint32_t  format;
        uint32_t width;
        uint32_t height;
        uint32_t field;

        int configure(int fd) const;
    };

    struct Crop {
        short pad;
        short stream;
        uint32_t left;
        uint32_t top;
        uint32_t width;
        uint32_t height;

        int configure(int fd) const;
    };

    struct Link {
        std::string name;
        short pads[2];
        bool active;

        int configure (std::string srcName, bool matchSubstr) const;
    };

    // Members
    const std::string name;
    const bool match_substr;
    const std::vector<v4l2_subdev_route> routes;
    const std::vector<Format> formats;
    const std::vector<Crop> crops;
    const std::vector<Link> links;
    std::string devNode;

    int SetRoutes(int fd, const std::vector<v4l2_subdev_route> &vecRoutes);
    bool configure();
};

int MediaCfg::SetRoutes(int fd, const std::vector<v4l2_subdev_route> &vecRoutes) {
    /* This is not a route's member method, because it operates on vector of routes. Besides that, there's no reason to make route our type. */
    int cnt = vecRoutes.size();
    if (cnt == 0)
        return 0;
	
	struct v4l2_subdev_client_capability clientcap = {};
    bool client_streams;
    clientcap.capabilities = V4L2_SUBDEV_CLIENT_CAP_STREAMS
			       | V4L2_SUBDEV_CLIENT_CAP_INTERVAL_USES_WHICH;

	int ret = ioctl(fd, VIDIOC_SUBDEV_S_CLIENT_CAP, &clientcap);
	client_streams = !ret && (clientcap.capabilities & V4L2_SUBDEV_CLIENT_CAP_STREAMS);


    v4l2_subdev_route routes[cnt];

    std::copy(vecRoutes.begin(), vecRoutes.end(), routes);

    v4l2_subdev_routing routing;
    routing.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    routing.routes = (uintptr_t)routes;
    routing.num_routes = cnt;
    routing.len_routes = cnt;

    ret = ioctl(fd, VIDIOC_SUBDEV_S_ROUTING, &routing);
    if ((ret < 0) && (errno == EBUSY)) {
        usleep(100000);
        ret = ioctl(fd, VIDIOC_SUBDEV_S_ROUTING, &routing);
    }
    if (ret < 0) {
        return -errno;
    }
    return 0;
}

int MediaCfg::Format::configure (int fd) const {
    v4l2_subdev_format sformat;

    memset(&sformat, 0, sizeof(sformat));
    sformat.pad = pad;
    sformat.stream = stream;
    sformat.which = V4L2_SUBDEV_FORMAT_ACTIVE;

    sformat.format.width = width;
    sformat.format.height = height;
    sformat.format.code = format;
    sformat.format.field = field;

    int ret = ioctl(fd, VIDIOC_SUBDEV_S_FMT, &sformat);
    if ((ret < 0) && (errno == EBUSY)) {
        usleep(100000);
        ret = ioctl(fd, VIDIOC_SUBDEV_S_FMT, &sformat);
    }
    if (ret < 0) {
        return -errno;
    }
    return 0;
}

int MediaCfg::Crop::configure (int fd) const {
    struct v4l2_subdev_crop crop;

    memset(&crop, 0, sizeof(crop));
	crop.pad = pad;
	crop.stream = stream;
	crop.which = V4L2_SUBDEV_FORMAT_ACTIVE;

	crop.rect.left = left;
	crop.rect.top = top;
	crop.rect.width = width;
	crop.rect.height = height;

	int ret = ioctl(fd, VIDIOC_SUBDEV_S_CROP, &crop);
    if ((ret < 0) && (errno == EBUSY)) {
        usleep(100000);
        ret = ioctl(fd, VIDIOC_SUBDEV_S_CROP, &crop);
    }
	if (ret < 0)
		return -errno;
    return 0;
}

int MediaCfg::Link::configure (std::string srcName, bool matchSubstr) const {
    media_link_desc link;
    for (unsigned i = 0; ; ++i) {
        unsigned toFind = 2;

        std::string mediaDevnode = "/dev/media";

        mediaDevnode += std::to_string(i);

        int fd = open(mediaDevnode.c_str(), O_RDWR);
        if (fd < 0) {
            LOG(WARNING) << "Failed to open media device " << mediaDevnode;
            break;
        }
        memset(&link, 0, sizeof(link));

        for (int id = 0; ; /* id++*/ ) {
            media_entity_desc entity;
            memset(&entity, 0, sizeof(entity));
            entity.id = id | MEDIA_ENT_ID_FLAG_NEXT;

            if (ioctl(fd, MEDIA_IOC_ENUM_ENTITIES, &entity) < 0)
                break;
            LOG(WARNING) << "Got entity[" << entity.id << "] " << entity.name;

            /* source pad */
            if (name == entity.name) {
                link.source.entity = entity.id;
                link.source.index = pads[0];
                link.source.flags = MEDIA_PAD_FL_SOURCE;
                --toFind;
            }
            /* sink pad */
            if (matchSubstr ?
                NULL != strstr(srcName.c_str(), entity.name)
                : (srcName == entity.name))
            {
                link.sink.entity = entity.id;
                link.sink.index = pads[1];
                link.sink.flags = MEDIA_PAD_FL_SINK;
                --toFind;
            }
            id = entity.id;
        }
        if (toFind > 0) // Continue before we find two id's. First for source and second for sink.
            continue;

        int ret = ioctl(fd, MEDIA_IOC_SETUP_LINK, &link);
        if ((ret < 0) && (errno == EBUSY)) {
            usleep(100000);
            ret = ioctl(fd, MEDIA_IOC_SETUP_LINK, &link);
        }
        if (ret == -1) {
            ret = -errno;
            LOG(ERROR) << "Failed to MEDIA_IOC_SETUP_LINK";
        }
        close(fd);
        return ret;
    }
    return 1;
}

bool MediaCfg::configure() {
    LOG(DEBUG) << "MediaCfg device " << name << " has devnode " << devNode;
    if (devNode.size() == 0)
        return -1;

    int ret;
    if (devNode.length() == 0) {
        LOG(ERROR) << "Failed to open devnode of " << name << " because its devNode had not been registered.";
        return -1;
    }

    int fd = open(devNode.c_str(), O_RDWR);
    if (fd < 0) {
        LOG(ERROR) << "Failed to open devnode " << devNode;
        return -1;
    }
    for (auto &link: links) {
        ret = link.configure(name, match_substr);
        if (ret != 0)
            LOG(ERROR) << "Failed to link.configure for " << name << " error: " << ret;
    }
    ret = SetRoutes(fd, routes);
    if (ret != 0)
        LOG(ERROR) << "Failed to SetRoutes for " << name << " error: " << ret;

    for (auto &format: formats) {
        ret = format.configure(fd);
        if (ret != 0)
            LOG(ERROR) << "Failed to format.configure for " << name << " error: " << ret;
    }
    for (auto &crop: crops) {
        ret = crop.configure(fd);
        if (ret != 0)
            LOG(ERROR) << "Failed to crop.configure for " << name << " error: " << ret;
    }
    close(fd);
    return 0;
}

#define UYVY8_1X16 MEDIA_BUS_FMT_UYVY8_1X16
#define RGB888_1X24 MEDIA_BUS_FMT_RGB888_1X24

std::vector<MediaCfg> cfgGroups[1] = {
    { /* Perhaps we will need to add another group of MediaCfg just like this cfgGroups[0] to the end for ov5640.
       * That group will then be adressed as cfgGroups[1].
       */
        MediaCfg{
            "max9286",
            true,
            { /* routes: max9286 routes each input pad to different stream on output pad 4. */
                // sink pad, sink stream, source pad, source stream, "ACTIVE"
                {   0,          0,          4,          0,              V4L2_SUBDEV_ROUTE_FL_ACTIVE},
                {   1,          0,          4,          1,              V4L2_SUBDEV_ROUTE_FL_ACTIVE},
                {   2,          0,          4,          2,              V4L2_SUBDEV_ROUTE_FL_ACTIVE},
                {   3,          0,          4,          3,              V4L2_SUBDEV_ROUTE_FL_ACTIVE}
            },
            { // formats
                // pad, stream, color,          width,  height, field
                {   0,  0,      UYVY8_1X16,     1280,   800,    V4L2_FIELD_NONE},
                {   1,  0,      UYVY8_1X16,     1280,   800,    V4L2_FIELD_NONE},
                {   2,  0,      UYVY8_1X16,     1280,   800,    V4L2_FIELD_NONE},
                {   3,  0,      UYVY8_1X16,     1280,   800,    V4L2_FIELD_NONE}
            },
            {}, // crop
            { // links
                // other side,          our pad, their pad, active
                {   "imx8mq-mipi-csi2 58227000.csi", {4,       0},       true }
            }
        },
        MediaCfg{
            "imx8mq-mipi-csi2 58227000.csi",
            false,
            {
                {0, 0,      1, 0,   true},
                {0, 1,      1, 1,   true},
                {0, 2,      1, 2,   true},
                {0, 3,      1, 3,   true}
            },
            { // pad, stream, color,    resolution, field
                {0, 0,  UYVY8_1X16,     1280, 800,  V4L2_FIELD_NONE},
                {0, 1,  UYVY8_1X16,     1280, 800,  V4L2_FIELD_NONE},
                {0, 2,  UYVY8_1X16,     1280, 800,  V4L2_FIELD_NONE},
                {0, 3,  UYVY8_1X16,     1280, 800,  V4L2_FIELD_NONE}
            },
            {}, // crop
            {}
        },

        MediaCfg{
            "crossbar",
            false,
            {
                {2, 0,  7, 0,   true}, // Stream[0] to pad[7] that is isi.1.
                {2, 1,  8, 0,   true},
                {2, 2,  9, 0,   true},
                {2, 3,  10, 0,  true}
            },
            { // pad, stream, color,    resolution, field
                {2, 0,  UYVY8_1X16,     1280, 800,  V4L2_FIELD_NONE},
                {2, 1,  UYVY8_1X16,     1280, 800,  V4L2_FIELD_NONE},
                {2, 2,  UYVY8_1X16,     1280, 800,  V4L2_FIELD_NONE},
                {2, 3,  UYVY8_1X16,     1280, 800,  V4L2_FIELD_NONE}
            },
            {}, // crop
            { // links
                {"imx8mq-mipi-csi2 58227000.csi", {4, 0}, true }
            }
        },

        // You have to name each these isi, since I had put device nodes in them, not vice versa - ie. 1 node to 1 here.
        MediaCfg{
            "mxc_isi.4",
            false,
            {},
            { // pad, stream, color,    resolution, field
                {0, 0,  UYVY8_1X16,     1280, 800,  V4L2_FIELD_NONE},
                {1, 0,  ISI_COLORSPACE,    1280, 800,  V4L2_FIELD_NONE} // EVS need RGB888_1X24 and camera UYVY8_1X16.
            },
            { // crop
                {1, 0,  0, 40, 1280, 720}
            },
            {}
        },

        MediaCfg{
            "mxc_isi.1",
            false,
            {},
            { // pad, stream, color,    resolution, field
                {0, 0,  UYVY8_1X16,     1280, 800,  V4L2_FIELD_NONE},
                {1, 0,  ISI_COLORSPACE,    1280, 800,  V4L2_FIELD_NONE}
            },
            { // crop
                {1, 0,  0, 40, 1280, 720}
            },
            {}
        },

        MediaCfg{
            "mxc_isi.2",
            false,
            {},
            { // pad, stream, color,    resolution, field
                {0, 0,  UYVY8_1X16,     1280, 800,  V4L2_FIELD_NONE},
                {1, 0,  ISI_COLORSPACE,    1280, 800,  V4L2_FIELD_NONE}
            },
            { // crop
                {1, 0,  0, 40, 1280, 720}
            },
            {}
        },

        MediaCfg{
            "mxc_isi.3",
            false,
            {},
            { // pad, stream, color,    resolution, field
                {0, 0,  UYVY8_1X16,     1280, 800,  V4L2_FIELD_NONE},
                {1, 0,  ISI_COLORSPACE,    1280, 800,  V4L2_FIELD_NONE}
            },
            { // crop
                {1, 0,  0, 40, 1280, 720}
            },
            {}
        }
    }
};

#undef UYVY8_1X16
#undef RGB888_1X24

bool configure(bool onlyIsiConfig) {
    bool retval = true;

    auto &cfgGroup = cfgGroups[0];
    for (auto &group : cfgGroup) {

        if (onlyIsiConfig) {
            std::vector<std::string> names = {"mxc_isi.1", "mxc_isi.2", "mxc_isi.3", "mxc_isi.4"};

            if (std::find(names.begin(), names.end(), group.name) == names.end()) {
                continue;
            }
            LOG(WARNING) << "conf " << group.name;
        }
        int ret = group.configure();

        if (ret != 0) {
            LOG(ERROR) << "Routing failed";
            retval = false;
        }
    }
    return retval;
}

bool registerDevnode(std::string name, std::string devNode) {
    auto &cfgGroup = cfgGroups[0];
    for (auto &group : cfgGroup) {
        int found = 0;

        if (group.match_substr) {
            found = NULL != strstr(name.c_str(), group.name.c_str());
        } else
            found = NULL != strstr(group.name.c_str(), name.c_str());

        if (found) {
            LOG(WARNING) << "registerDevnode " << name;
            group.devNode = devNode;
            return true;
        }
    }
    return false;
}
