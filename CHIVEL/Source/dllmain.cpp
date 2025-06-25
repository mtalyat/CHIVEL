// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <Python.h>
#include <Windows.h>
#include <filesystem>

#pragma region CHIVEL

namespace chivel
{
    std::string trim(std::string str)
    {
		// Trim leading whitespace and trailing whitespace
		size_t first = str.find_first_not_of(" \t\n\r\f\v");
		size_t last = str.find_last_not_of(" \t\n\r\f\v");
		if (first == std::string::npos || last == std::string::npos) {
			return ""; // String is all whitespace
		}
		return str.substr(first, last - first + 1);
    }

    cv::Mat captureScreen(int monitorIndex = 0) {
        // Get all monitor info
        DISPLAY_DEVICE dd;
        dd.cb = sizeof(dd);
        EnumDisplayDevices(NULL, monitorIndex, &dd, 0);

        DEVMODE dm;
        dm.dmSize = sizeof(dm);
        EnumDisplaySettings(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm);

        int width = dm.dmPelsWidth;
        int height = dm.dmPelsHeight;

        // Create device contexts
        HDC hScreenDC = CreateDC(NULL, dd.DeviceName, NULL, NULL);
        HDC hMemoryDC = CreateCompatibleDC(hScreenDC);

        // Create a compatible bitmap
        HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

        // Copy screen to bitmap
        BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, 0, 0, SRCCOPY);

        // Create OpenCV image
        BITMAPINFOHEADER bi = { 0 };
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = width;
        bi.biHeight = -height; // negative for top-down bitmap
        bi.biPlanes = 1;
        bi.biBitCount = 24;
        bi.biCompression = BI_RGB;

        cv::Mat mat(height, width, CV_8UC3);
        GetDIBits(hMemoryDC, hBitmap, 0, height, mat.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

        // Cleanup
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        DeleteDC(hScreenDC);

        return mat;
    }

    cv::Mat adjustImage(cv::Mat const& original)
    {
        cv::Mat gray;
        if (original.channels() == 3) {
            cv::cvtColor(original, gray, cv::COLOR_BGR2GRAY);
        }
        else if (original.channels() == 1) {
            gray = original;
        }
        else {
            // Unsupported format
            return cv::Mat();
        }

        // scale image
        cv::Mat resized;
        float const scale = 4.0f; // Scale factor for better OCR
        cv::resize(gray, resized, cv::Size(), scale, scale, cv::INTER_LINEAR);

        //// Apply adaptive thresholding for better OCR
        //cv::Mat processed;
        //cv::adaptiveThreshold(gray, processed, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY, 15, 10);

        cv::Mat final = resized;
        return final;
    }

    cv::Mat readImage(char const* const path)
    {
		return cv::imread(path, cv::IMREAD_COLOR);
    }

	bool setCursorPosition(int x, int y) {
		return SetCursorPos(x, y);
	}
}

#pragma endregion

#pragma region Python

#pragma region Image

typedef struct {
    PyObject_HEAD
        cv::Mat* mat;
} CHIVELImageObject;

static void CHIVELImage_dealloc(CHIVELImageObject* self) {
    delete self->mat;
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* CHIVELImage_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
    CHIVELImageObject* self = (CHIVELImageObject*)type->tp_alloc(type, 0);
    if (self != nullptr) {
        self->mat = new cv::Mat();
    }
    return (PyObject*)self;
}

static int CHIVELImage_init(CHIVELImageObject* self, PyObject* args, PyObject* kwds) {
    // Initialize the cv::Mat object here if needed
    return 0;
}

static PyTypeObject CHIVELImageType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "CHIVEL.Image",             /* tp_name */
    sizeof(CHIVELImageObject),  /* tp_basicsize */
    0,                         /* tp_itemsize */
    (destructor)CHIVELImage_dealloc, /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_reserved */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash  */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT |
    Py_TPFLAGS_BASETYPE,       /* tp_flags */
    "CHIVEL Image objects",     /* tp_doc */
    0,		                   /* tp_traverse */
    0,		                   /* tp_clear */
    0,		                   /* tp_richcompare */
    0,		                   /* tp_weaklistoffset */
    0,		                   /* tp_iter */
    0,		                   /* tp_iternext */
    0,                         /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)CHIVELImage_init, /* tp_init */
    0,                         /* tp_alloc */
    CHIVELImage_new,            /* tp_new */
};


#pragma endregion

static PyObject* chivel_load(PyObject* self, PyObject* args) {
    const char* path;
    if (!PyArg_ParseTuple(args, "s", &path))
        return nullptr;

    // Load image using OpenCV
    cv::Mat img = chivel::readImage(path);
    if (img.empty()) {
        PyErr_SetString(PyExc_IOError, "Failed to load image from path");
        return nullptr;
    }

    // Create a new CHIVEL.Image object
    PyObject* image_obj = CHIVELImage_new(&CHIVELImageType, nullptr, nullptr);
    if (!image_obj)
        return nullptr;

    CHIVELImageObject* image = (CHIVELImageObject*)image_obj;
    delete image->mat; // Delete the default empty mat
    image->mat = new cv::Mat(img); // Assign loaded image

    return image_obj;
}

static PyObject* chivel_save(PyObject* self, PyObject* args) {
    PyObject* image_obj;
    const char* path;

    if (!PyArg_ParseTuple(args, "Os", &image_obj, &path))
        return nullptr;

    if (!PyObject_TypeCheck(image_obj, &CHIVELImageType)) {
        PyErr_SetString(PyExc_TypeError, "First argument must be a CHIVEL.Image object");
        return nullptr;
    }

    CHIVELImageObject* image = (CHIVELImageObject*)image_obj;
    if (!image->mat || image->mat->empty()) {
        PyErr_SetString(PyExc_ValueError, "Image data is empty");
        return nullptr;
    }

    if (!cv::imwrite(path, *(image->mat))) {
        PyErr_SetString(PyExc_IOError, "Failed to save image to path");
        return nullptr;
    }

    Py_RETURN_NONE;
}

static PyObject* chivel_show(PyObject* self, PyObject* args) {
    PyObject* image_obj;
    const char* window_name = "Image";
    if (!PyArg_ParseTuple(args, "O|s", &image_obj, &window_name))
        return nullptr;

    // Check if the object is of type CHIVEL.Image
    if (!PyObject_TypeCheck(image_obj, &CHIVELImageType)) {
        PyErr_SetString(PyExc_TypeError, "Expected a CHIVEL.Image object");
        return nullptr;
    }

    CHIVELImageObject* image = (CHIVELImageObject*)image_obj;
    if (!image->mat || image->mat->empty()) {
        PyErr_SetString(PyExc_ValueError, "Image data is empty");
        return nullptr;
    }

    // Show the image using OpenCV
    cv::imshow(window_name, *(image->mat));
    cv::waitKey(0); // Wait for a key press

    Py_RETURN_NONE;
}

static PyObject* chivel_capture(PyObject* self, PyObject* args) {
    int monitorIndex = 0;
    if (!PyArg_ParseTuple(args, "|i", &monitorIndex))
        return nullptr;

    // Capture the screen using your existing function
    cv::Mat img = chivel::captureScreen(monitorIndex);
    if (img.empty()) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to capture screen for the given display index");
        return nullptr;
    }

    // Create a new CHIVEL.Image object
    PyObject* image_obj = CHIVELImage_new(&CHIVELImageType, nullptr, nullptr);
    if (!image_obj)
        return nullptr;

    CHIVELImageObject* image = (CHIVELImageObject*)image_obj;
    delete image->mat; // Delete the default empty mat
    image->mat = new cv::Mat(img); // Assign captured image

    return image_obj;
}

static std::filesystem::path get_module_dir()
{
    wchar_t path[MAX_PATH];
    HMODULE hModule = NULL;
    // Get handle to the current module (NULL = this DLL)
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&get_module_dir, &hModule)) {
        GetModuleFileNameW(hModule, path, MAX_PATH);
        return std::filesystem::path(path).parent_path();
    }
    // fallback: current working directory
    return std::filesystem::current_path();
}

static PyObject* chivel_find(PyObject* self, PyObject* args, PyObject* kwargs) {
    PyObject* source_obj;
    PyObject* search_obj;
    double threshold = 0.8; // Default threshold for match quality
    int level = tesseract::RIL_PARA; // Default to PARA

    static const char* kwlist[] = { "source", "search", "threshold", "level", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|di", (char**)kwlist, &source_obj, &search_obj, &threshold, &level))
        return nullptr;

    if (!PyObject_TypeCheck(source_obj, &CHIVELImageType)) {
        PyErr_SetString(PyExc_TypeError, "First argument must be a CHIVEL.Image object");
        return nullptr;
    }

    CHIVELImageObject* source = (CHIVELImageObject*)source_obj;
    if (!source->mat || source->mat->empty()) {
        PyErr_SetString(PyExc_ValueError, "Source image is empty");
        return nullptr;
    }

    // If second arg is CHIVEL.Image, do template matching
    if (PyObject_TypeCheck(search_obj, &CHIVELImageType)) {
        CHIVELImageObject* templ = (CHIVELImageObject*)search_obj;
        if (!templ->mat || templ->mat->empty()) {
            PyErr_SetString(PyExc_ValueError, "Template image is empty");
            return nullptr;
        }

        cv::Mat result;
        int result_cols = source->mat->cols - templ->mat->cols + 1;
        int result_rows = source->mat->rows - templ->mat->rows + 1;
        if (result_cols <= 0 || result_rows <= 0) {
            PyErr_SetString(PyExc_ValueError, "Template image is larger than source image");
            return nullptr;
        }
        result.create(result_rows, result_cols, CV_32FC1);

        cv::matchTemplate(*(source->mat), *(templ->mat), result, cv::TM_CCOEFF_NORMED);

        std::vector<cv::Rect> rects;
        std::vector<int> weights;

        double minVal, maxVal;
        cv::Point minLoc, maxLoc;
        cv::Mat mask = cv::Mat::ones(result.size(), CV_8U);

        while (true) {
            cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc, mask);
            if (maxVal < threshold)
                break;

            int x = maxLoc.x;
            int y = maxLoc.y;
            int w = templ->mat->cols;
            int h = templ->mat->rows;

            rects.push_back(cv::Rect(x, y, w, h));

            // Suppress this region in the mask to avoid duplicate matches
            cv::Rect region(x, y, w, h);
            region &= cv::Rect(0, 0, mask.cols, mask.rows);
            mask(region) = 0;
        }

        // Group similar rectangles
        if (!rects.empty()) {
            cv::groupRectangles(rects, weights, 1, 0.5);
        }

        PyObject* matches = PyList_New(0);
        for (const auto& r : rects) {
            PyObject* tuple = Py_BuildValue("(iiii)", r.x, r.y, r.width, r.height);
            PyList_Append(matches, tuple);
            Py_DECREF(tuple);
        }
        return matches;
    }

    // If second arg is a string, do OCR and search for the text
    if (PyUnicode_Check(search_obj)) {
        PyObject* py_search_str = PyUnicode_AsUTF8String(search_obj);
        if (!py_search_str) {
            PyErr_SetString(PyExc_TypeError, "Failed to convert search string to UTF-8");
            return nullptr;
        }
        const char* search = PyBytes_AsString(py_search_str);
        if (!search) {
            Py_DECREF(py_search_str);
            PyErr_SetString(PyExc_TypeError, "Failed to get search string bytes");
            return nullptr;
        }
		std::string search_str(chivel::trim(search));

		cv::Mat original = *(source->mat);
		int width = original.cols;
		int height = original.rows;
		cv::Mat src = chivel::adjustImage(original);

        tesseract::TessBaseAPI tess;
		std::filesystem::path tessdata_path = get_module_dir() / "tessdata";
        if (tess.Init(tessdata_path.string().c_str(), "eng", tesseract::OEM_LSTM_ONLY) != 0) {
            Py_DECREF(py_search_str);
            PyErr_SetString(PyExc_RuntimeError, "Could not initialize tesseract.");
            return nullptr;
        }
        tess.SetPageSegMode(tesseract::PSM_SPARSE_TEXT);
		tess.SetVariable("user_defined_dpi", "300");
        tess.SetImage(src.data, src.cols, src.rows, 1, static_cast<int>(src.step));
        tess.Recognize(nullptr);
        tesseract::ResultIterator* ri = tess.GetIterator();
        tesseract::PageIteratorLevel pil = static_cast<tesseract::PageIteratorLevel>(level);

        double scaleX = static_cast<double>(width) / src.cols;
        double scaleY = static_cast<double>(height) / src.rows;

        PyObject* matches = PyList_New(0);
        if (ri != nullptr) {
            do {
                const char* word = ri->GetUTF8Text(pil);
				std::string word_str(word ? word : "");
				word_str = chivel::trim(word_str);

				float conf = ri->Confidence(pil);
				if (!word || conf < threshold * 100.0f) {
					delete[] word; // Clean up if no word or invalid confidence
					continue;
				}
				// Scale bounding box coordinates
				int x1, y1, x2, y2;
				if (ri->BoundingBox(pil, &x1, &y1, &x2, &y2)) {
                    x1 = static_cast<int>(x1 * scaleX);
                    y1 = static_cast<int>(y1 * scaleY);
                    x2 = static_cast<int>(x2 * scaleX);
                    y2 = static_cast<int>(y2 * scaleY);
					if (word_str == search_str) {
                        // word found
						PyObject* rect_tuple = Py_BuildValue("(iiii)", x1, y1, x2 - x1, y2 - y1);
						PyList_Append(matches, rect_tuple);
						Py_DECREF(rect_tuple);
					}
				}
                if (word) delete[] word;
            } while (ri->Next(pil));
        }

        Py_DECREF(py_search_str);
        return matches;
    }

    PyErr_SetString(PyExc_TypeError, "Second argument must be a CHIVEL.Image or a string");
    return nullptr;
}

static PyObject* chivel_draw(PyObject* self, PyObject* args, PyObject* kwds) {
    PyObject* image_obj;
    PyObject* rects_obj;
    PyObject* color_obj = nullptr;

    static const char* kwlist[] = { "image", "rects", "color", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "OO|O", (char**)kwlist, &image_obj, &rects_obj, &color_obj))
        return nullptr;

    if (!PyObject_TypeCheck(image_obj, &CHIVELImageType)) {
        PyErr_SetString(PyExc_TypeError, "First argument must be a CHIVEL.Image object");
        return nullptr;
    }

    CHIVELImageObject* image = (CHIVELImageObject*)image_obj;
    if (!image->mat || image->mat->empty()) {
        PyErr_SetString(PyExc_ValueError, "Image data is empty");
        return nullptr;
    }

    // Parse color
    cv::Scalar color(0, 0, 255); // Default: red (BGR)
    if (color_obj && PyTuple_Check(color_obj) && PyTuple_Size(color_obj) == 3) {
        int b = 0, g = 0, r = 0;
        if (!PyArg_ParseTuple(color_obj, "iii", &r, &g, &b)) {
            PyErr_SetString(PyExc_TypeError, "Color must be a tuple of 3 integers");
            return nullptr;
        }
        color = cv::Scalar(b, g, r);
    }

    // Helper to draw a single rectangle
    auto draw_rect = [&](PyObject* tuple) {
        if (!PyTuple_Check(tuple) || PyTuple_Size(tuple) != 4)
            return false;
        int x, y, w, h;
        if (!PyArg_ParseTuple(tuple, "iiii", &x, &y, &w, &h))
            return false;
        cv::rectangle(*(image->mat), cv::Rect(x, y, w, h), color, 2);
        return true;
        };

    // Accept either a tuple or a list of tuples
    if (PyTuple_Check(rects_obj) && PyTuple_Size(rects_obj) == 4) {
        if (!draw_rect(rects_obj)) {
            PyErr_SetString(PyExc_TypeError, "Rectangle must be a tuple of 4 integers");
            return nullptr;
        }
    }
    else if (PyList_Check(rects_obj)) {
        Py_ssize_t n = PyList_Size(rects_obj);
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject* item = PyList_GetItem(rects_obj, i);
            if (!draw_rect(item)) {
                PyErr_SetString(PyExc_TypeError, "Each rectangle must be a tuple of 4 integers");
                return nullptr;
            }
        }
    }
    else {
        PyErr_SetString(PyExc_TypeError, "rects must be a tuple (x, y, w, h) or a list of such tuples");
        return nullptr;
    }

    Py_RETURN_NONE;
}

static PyObject* chivel_mouse_move(PyObject* self, PyObject* args) {
    int monitor_index;
    PyObject* pos_obj;

    if (!PyArg_ParseTuple(args, "iO", &monitor_index, &pos_obj))
        return nullptr;

    // Get monitor info
    DISPLAY_DEVICE dd;
    dd.cb = sizeof(dd);
    if (!EnumDisplayDevices(NULL, monitor_index, &dd, 0)) {
        PyErr_SetString(PyExc_ValueError, "Invalid monitor index");
        return nullptr;
    }

    DEVMODE dm;
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettings(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to get monitor settings");
        return nullptr;
    }

    int mon_x = dm.dmPosition.x;
    int mon_y = dm.dmPosition.y;

    int x = 0, y = 0;
    if (PyTuple_Check(pos_obj)) {
        Py_ssize_t size = PyTuple_Size(pos_obj);
        if (size == 2) {
            if (!PyArg_ParseTuple(pos_obj, "ii", &x, &y)) {
                PyErr_SetString(PyExc_TypeError, "Position must be a tuple of 2 integers (x, y)");
                return nullptr;
            }
        }
        else if (size == 4) {
            int w = 0, h = 0;
            if (!PyArg_ParseTuple(pos_obj, "iiii", &x, &y, &w, &h)) {
                PyErr_SetString(PyExc_TypeError, "Rectangle must be a tuple of 4 integers (x, y, w, h)");
                return nullptr;
            }
            x += w / 2;
            y += h / 2;
        }
        else {
            PyErr_SetString(PyExc_TypeError, "Tuple must be (x, y) or (x, y, w, h)");
            return nullptr;
        }
    }
    else {
        PyErr_SetString(PyExc_TypeError, "Position argument must be a tuple");
        return nullptr;
    }

    int abs_x = mon_x + x;
    int abs_y = mon_y + y;

    if (!chivel::setCursorPosition(abs_x, abs_y)) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to move mouse cursor");
        return nullptr;
    }

    Py_RETURN_NONE;
}

static PyObject* chivel_mouse_click(PyObject* self, PyObject* args, PyObject* kwds) {
    int button = 0;
    int count = 1;
    static const char* kwlist[] = { "button", "count", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ii", (char**)kwlist, &button, &count))
        return nullptr;

    if (count < 1) {
        PyErr_SetString(PyExc_ValueError, "Count must be >= 1");
        return nullptr;
    }

    INPUT inputs[2] = {};
    inputs[0].type = INPUT_MOUSE;
    inputs[1].type = INPUT_MOUSE;

    switch (button) {
    case 0: // Left
        inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        break;
    case 1: // Right
        inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
        inputs[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        break;
    case 2: // Middle
        inputs[0].mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
        inputs[1].mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
        break;
    case 3: // X1 (Button 4)
        inputs[0].mi.dwFlags = MOUSEEVENTF_XDOWN;
        inputs[1].mi.dwFlags = MOUSEEVENTF_XUP;
        inputs[0].mi.mouseData = XBUTTON1;
        inputs[1].mi.mouseData = XBUTTON1;
        break;
    case 4: // X2 (Button 5)
        inputs[0].mi.dwFlags = MOUSEEVENTF_XDOWN;
        inputs[1].mi.dwFlags = MOUSEEVENTF_XUP;
        inputs[0].mi.mouseData = XBUTTON2;
        inputs[1].mi.mouseData = XBUTTON2;
        break;
    default:
        PyErr_SetString(PyExc_ValueError, "Button must be 1 (left), 2 (right), 3 (middle), 4 (X1), or 5 (X2)");
        return nullptr;
    }

    for (int i = 0; i < count; ++i) {
        if (SendInput(2, inputs, sizeof(INPUT)) != 2) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to send mouse click event");
            return nullptr;
        }
    }

    Py_RETURN_NONE;
}

static PyObject* chivel_mouse_down(PyObject* self, PyObject* args) {
    int button = 0;
    if (!PyArg_ParseTuple(args, "|i", &button))
        return nullptr;

    INPUT input = {};
    input.type = INPUT_MOUSE;

    switch (button) {
    case 0: // Left
        input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        break;
    case 1: // Right
        input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
        break;
    case 2: // Middle
        input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
        break;
    case 3: // X1 (Button 4)
        input.mi.dwFlags = MOUSEEVENTF_XDOWN;
        input.mi.mouseData = XBUTTON1;
        break;
    case 4: // X2 (Button 5)
        input.mi.dwFlags = MOUSEEVENTF_XDOWN;
        input.mi.mouseData = XBUTTON2;
        break;
    default:
        PyErr_SetString(PyExc_ValueError, "Button must be 1 (left), 2 (right), 3 (middle), 4 (X1), or 5 (X2)");
        return nullptr;
    }

    if (SendInput(1, &input, sizeof(INPUT)) != 1) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to send mouse down event");
        return nullptr;
    }

    Py_RETURN_NONE;
}

static PyObject* chivel_mouse_up(PyObject* self, PyObject* args) {
    int button = 0;
    if (!PyArg_ParseTuple(args, "|i", &button))
        return nullptr;

    INPUT input = {};
    input.type = INPUT_MOUSE;

    switch (button) {
    case 0: // Left
        input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        break;
    case 1: // Right
        input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        break;
    case 2: // Middle
        input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
        break;
    case 3: // X1 (Button 4)
        input.mi.dwFlags = MOUSEEVENTF_XUP;
        input.mi.mouseData = XBUTTON1;
        break;
    case 4: // X2 (Button 5)
        input.mi.dwFlags = MOUSEEVENTF_XUP;
        input.mi.mouseData = XBUTTON2;
        break;
    default:
        PyErr_SetString(PyExc_ValueError, "Button must be 1 (left), 2 (right), 3 (middle), 4 (X1), or 5 (X2)");
        return nullptr;
    }

    if (SendInput(1, &input, sizeof(INPUT)) != 1) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to send mouse up event");
        return nullptr;
    }

    Py_RETURN_NONE;
}

static PyObject* chivel_mouse_scroll(PyObject* self, PyObject* args, PyObject* kwds) {
    int vertical = 0;
    int horizontal = 0;
    static const char* kwlist[] = { "vertical", "horizontal", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i|i", (char**)kwlist, &vertical, &horizontal))
        return nullptr;

    // At least one scroll direction must be nonzero
    if (vertical == 0 && horizontal == 0) {
        PyErr_SetString(PyExc_ValueError, "At least one of vertical or horizontal must be nonzero");
        return nullptr;
    }

    // Each wheel "notch" is WHEEL_DELTA (120)
    INPUT input = {};
    input.type = INPUT_MOUSE;

    // Vertical scroll
    if (vertical != 0) {
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = vertical * WHEEL_DELTA;
        if (SendInput(1, &input, sizeof(INPUT)) != 1) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to send vertical mouse wheel event");
            return nullptr;
        }
    }

    // Horizontal scroll
    if (horizontal != 0) {
        input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        input.mi.mouseData = horizontal * WHEEL_DELTA;
        if (SendInput(1, &input, sizeof(INPUT)) != 1) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to send horizontal mouse wheel event");
            return nullptr;
        }
    }

    Py_RETURN_NONE;
}

static PyObject* chivel_type(PyObject* self, PyObject* args, PyObject* kwds) {
    const char* text;
    double wait = 0.05;
    static const char* kwlist[] = { "text", "wait", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|d", (char**)kwlist, &text, &wait))
        return nullptr;

    if (!text) {
        PyErr_SetString(PyExc_ValueError, "Text must not be null");
        return nullptr;
    }

    // Convert to wide string for Unicode support
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (wlen <= 1) {
        PyErr_SetString(PyExc_ValueError, "Failed to convert text to wide string");
        return nullptr;
    }
    std::wstring wtext(wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, &wtext[0], wlen);

    DWORD ms = static_cast<DWORD>(wait * 1000.0);
    for (size_t i = 0; i < wtext.size(); i++)
    {
        wchar_t ch = wtext[i];

        // Prepare a KEYBDINPUT for the character
        INPUT input[2] = {};
        input[0].type = INPUT_KEYBOARD;
        input[0].ki.wVk = 0;
        input[0].ki.wScan = ch;
        input[0].ki.dwFlags = KEYEVENTF_UNICODE;

        input[1] = input[0];
        input[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

        // Send key down and key up
        SendInput(2, input, sizeof(INPUT));

        // Wait between keys
        if (i < wtext.size() - 1) // Don't wait after the last character
        {
            Sleep(ms);
        }
    }

    Py_RETURN_NONE;
}

static PyObject* chivel_wait(PyObject* self, PyObject* args) {
    double seconds;
    if (!PyArg_ParseTuple(args, "d", &seconds))
        return nullptr;

    if (seconds < 0) {
        PyErr_SetString(PyExc_ValueError, "Seconds must be non-negative");
        return nullptr;
    }

    DWORD ms = static_cast<DWORD>(seconds * 1000.0);
    Sleep(ms);

    Py_RETURN_NONE;
}

// Module initialization
static int chivel_module_exec(PyObject* module)
{
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);

    if (PyType_Ready(&CHIVELImageType) < 0)
        return -1;
    Py_INCREF(&CHIVELImageType);
    if (PyModule_AddObject(module, "Image", (PyObject*)&CHIVELImageType) < 0) {
        Py_DECREF(&CHIVELImageType);
        return -1;
    }

    PyModule_AddIntConstant(module, "BLOCK", tesseract::RIL_BLOCK);
    PyModule_AddIntConstant(module, "PARAGRAPH", tesseract::RIL_PARA);
    PyModule_AddIntConstant(module, "LINE", tesseract::RIL_TEXTLINE);
    PyModule_AddIntConstant(module, "WORD", tesseract::RIL_WORD);
    PyModule_AddIntConstant(module, "SYMBOL", tesseract::RIL_SYMBOL);

    return 0;
}

// Module deinitialization
static void chivel_module_free(void* module)
{
}

#pragma endregion

static PyModuleDef_Slot chivel_slots[] = {
	{Py_mod_exec, chivel_module_exec},
	{0, nullptr}
};

// Method definition object
static PyMethodDef chivelMethods[] = {
	{"load", chivel_load, METH_VARARGS, "Load an image from a file"},
	{"save", chivel_save, METH_VARARGS, "Save an image to a file"},
	{"show", chivel_show, METH_VARARGS, "Show an image in a window"},
	{"capture", chivel_capture, METH_VARARGS, "Capture the screen from a specific monitor"},
	{"find", (PyCFunction)chivel_find, METH_VARARGS | METH_KEYWORDS, "Find rectangles or text in an image"},
	{"draw", (PyCFunction)chivel_draw, METH_VARARGS | METH_KEYWORDS, "Draw rectangle(s) on an image"},
	{"wait", chivel_wait, METH_VARARGS, "Wait for a specified number of seconds"},
	{"mouse_move", chivel_mouse_move, METH_VARARGS, "Move the mouse cursor to a position on a specific monitor"},
	{"mouse_click", (PyCFunction)chivel_mouse_click, METH_VARARGS | METH_KEYWORDS, "Click the mouse button"},
	{"mouse_down", chivel_mouse_down, METH_VARARGS, "Press the given mouse button down"},
	{"mouse_up", chivel_mouse_up, METH_VARARGS, "Release the given mouse button"},
	{"mouse_scroll", (PyCFunction)chivel_mouse_scroll, METH_VARARGS | METH_KEYWORDS, "Scroll the mouse wheel vertically and/or horizontally"},
	{"type", (PyCFunction)chivel_type, METH_VARARGS | METH_KEYWORDS, "Type a string using the keyboard"},
    {nullptr, nullptr, 0, nullptr}
};

// Module definition
static struct PyModuleDef chivelmodule = {
    PyModuleDef_HEAD_INIT,
    "CHIVEL",   // Module name
    nullptr,     // Module documentation
    0,          // Size of per-interpreter state of the module
    chivelMethods,
    chivel_slots,
    nullptr,
    nullptr,
	chivel_module_free
};

// Module initialization function
PyMODINIT_FUNC PyInit_CHIVEL(void) {
    //return PyModule_Create(&chivelmodule);
	return PyModuleDef_Init(&chivelmodule);
}
