// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <Python.h>
#include <Windows.h>
#include <filesystem>
#include <regex>

#pragma region chivel

enum SimplifyMode
{
	SIMPLIFY_NONE = 0,
	SIMPLIFY_MOVE = 1,
	SIMPLIFY_CLICK = 2,
	SIMPLIFY_TYPE = 4,
    SIMPLIFY_TIME = 8,
	SIMPLIFY_ALL = SIMPLIFY_MOVE | SIMPLIFY_CLICK | SIMPLIFY_TYPE | SIMPLIFY_TIME
};

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

    cv::Mat captureRect(int x, int y, int w, int h, int monitorIndex = 0) {
        // Get monitor info
        DISPLAY_DEVICE dd;
        dd.cb = sizeof(dd);
        EnumDisplayDevices(NULL, monitorIndex, &dd, 0);

        DEVMODE dm;
        dm.dmSize = sizeof(dm);
        EnumDisplaySettings(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm);

        int mon_x = dm.dmPosition.x;
        int mon_y = dm.dmPosition.y;

        // Get the device context of the specific monitor
        HDC hScreenDC = CreateDC(NULL, dd.DeviceName, NULL, NULL);
        HDC hMemoryDC = CreateCompatibleDC(hScreenDC);

        // Create a compatible bitmap
        HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, w, h);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

        // Copy the specified rectangle into the bitmap (relative to monitor)
        BitBlt(hMemoryDC, 0, 0, w, h, hScreenDC, x, y, SRCCOPY);

        // Create OpenCV image
        BITMAPINFOHEADER bi = { 0 };
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = w;
        bi.biHeight = -h; // negative for top-down bitmap
        bi.biPlanes = 1;
        bi.biBitCount = 24;
        bi.biCompression = BI_RGB;

        cv::Mat mat(h, w, CV_8UC3);
        GetDIBits(hMemoryDC, hBitmap, 0, h, mat.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

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

    int get_display_count() {
        int count = 0;
        EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR, HDC, LPRECT, LPARAM lParam) -> BOOL {
                int* pCount = reinterpret_cast<int*>(lParam);
                ++(*pCount);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&count)
        );
        return count;
    }

    static BOOL CALLBACK monitor_enum_proc(HMONITOR hMonitor, HDC, LPRECT lprcMonitor, LPARAM dwData) {
        struct MonitorSearch {
            POINT pt;
            int index;
            int found;
            int x, y;
        };
        MonitorSearch* search = reinterpret_cast<MonitorSearch*>(dwData);
        if (PtInRect(lprcMonitor, search->pt)) {
            search->found = search->index;
            search->x = search->pt.x - lprcMonitor->left;
            search->y = search->pt.y - lprcMonitor->top;
            return FALSE; // Stop enumeration
        }
        search->index++;
        return TRUE; // Continue enumeration
    }

    bool run_python_play_function(const std::string& script_path, const std::string& play_func_name = "play")
    {
        // Initialize Python if needed
        if (!Py_IsInitialized())
            Py_Initialize();

        // Extract directory and module name
        std::filesystem::path path(script_path);
        std::filesystem::path dir = path.parent_path();
        std::filesystem::path stem = path.stem(); // filename without extension

        // Convert to UTF-8 for Python API
        std::string dir_utf8 = std::filesystem::path(dir).string();
        std::string mod_utf8 = std::filesystem::path(stem).string();

        // Add script directory to sys.path
        PyObject* sys_path = PySys_GetObject("path");
        PyObject* dir_py = PyUnicode_FromString(dir_utf8.c_str());
        PyList_Insert(sys_path, 0, dir_py);
        Py_DECREF(dir_py);

        // Import the module
        PyObject* module = PyImport_ImportModule(mod_utf8.c_str());
        if (!module) {
            PyErr_Print();
            return false;
        }

        // Get the 'play' function
        PyObject* func = PyObject_GetAttrString(module, play_func_name.c_str());
        if (!func || !PyCallable_Check(func)) {
            PyErr_Print();
            Py_XDECREF(func);
            Py_DECREF(module);
            return false;
        }

        // Call the function (no arguments)
        PyObject* result = PyObject_CallObject(func, nullptr);
        if (!result) {
            PyErr_Print();
            Py_DECREF(func);
            Py_DECREF(module);
            return false;
        }

        // Clean up
        Py_DECREF(result);
        Py_DECREF(func);
        Py_DECREF(module);

        // Optionally: Py_Finalize(); // Only if you are done with Python in the process

        return true;
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

static PyObject* CHIVELImage_get_size(CHIVELImageObject* self, PyObject* /*unused*/) {
    if (!self->mat || self->mat->empty()) {
        PyErr_SetString(PyExc_ValueError, "Image data is empty");
        return nullptr;
    }
    int width = self->mat->cols;
    int height = self->mat->rows;
    return Py_BuildValue("(ii)", width, height);
}

static PyMethodDef CHIVELImage_methods[] = {
    {"get_size", (PyCFunction)CHIVELImage_get_size, METH_NOARGS, "Return (width, height) of the image"},
    {nullptr, nullptr, 0, nullptr}
};

static PyTypeObject CHIVELImageType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "chivel.Image",             /* tp_name */
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
    "Chivel Image objects",    /* tp_doc */
    0,		                   /* tp_traverse */
    0,		                   /* tp_clear */
    0,		                   /* tp_richcompare */
    0,		                   /* tp_weaklistoffset */
    0,		                   /* tp_iter */
    0,		                   /* tp_iternext */
    CHIVELImage_methods,       /* tp_methods */
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

    // Create a new chivel.Image object
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
        PyErr_SetString(PyExc_TypeError, "First argument must be a chivel.Image object");
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

    // Check if the object is of type chivel.Image
    if (!PyObject_TypeCheck(image_obj, &CHIVELImageType)) {
        PyErr_SetString(PyExc_TypeError, "Expected a chivel.Image object");
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

static PyObject* chivel_capture(PyObject* self, PyObject* args, PyObject* kwargs) {
    int monitorIndex = 0;
    PyObject* rect_obj = nullptr;
    static const char* kwlist[] = { "monitor", "rect", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|iO", (char**)kwlist, &monitorIndex, &rect_obj))
        return nullptr;

    cv::Mat img;
    if (rect_obj && rect_obj != Py_None) {
        // Expect a tuple (x, y, w, h)
        int x, y, w, h;
        if (!PyTuple_Check(rect_obj) || PyTuple_Size(rect_obj) != 4 ||
            !PyArg_ParseTuple(rect_obj, "iiii", &x, &y, &w, &h)) {
            PyErr_SetString(PyExc_TypeError, "rect must be a tuple (x, y, w, h)");
            return nullptr;
        }
        img = chivel::captureRect(x, y, w, h, monitorIndex);
    }
    else {
        img = chivel::captureScreen(monitorIndex);
    }

    if (img.empty()) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to capture screen");
        return nullptr;
    }

    PyObject* image_obj = CHIVELImage_new(&CHIVELImageType, nullptr, nullptr);
    if (!image_obj)
        return nullptr;

    CHIVELImageObject* image = (CHIVELImageObject*)image_obj;
    delete image->mat;
    image->mat = new cv::Mat(img);

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

    static const char* kwlist[] = { "source", "search", "threshold", "text_level", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|di", (char**)kwlist, &source_obj, &search_obj, &threshold, &level))
        return nullptr;

    if (!PyObject_TypeCheck(source_obj, &CHIVELImageType)) {
        PyErr_SetString(PyExc_TypeError, "First argument must be a chivel.Image object");
        return nullptr;
    }

    CHIVELImageObject* source = (CHIVELImageObject*)source_obj;
    if (!source->mat || source->mat->empty()) {
        PyErr_SetString(PyExc_ValueError, "Source image is empty");
        return nullptr;
    }

    // If second arg is chivel.Image, do template matching
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
		//printf("Searching for: '%s'\n", search_str.c_str());
		std::regex search_regex(search_str);

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
                printf("Found word: '%s' with confidence %.2f\n", word_str.c_str(), conf);
				// Scale bounding box coordinates
				int x1, y1, x2, y2;
				if (ri->BoundingBox(pil, &x1, &y1, &x2, &y2)) {
                    x1 = static_cast<int>(x1 * scaleX);
                    y1 = static_cast<int>(y1 * scaleY);
                    x2 = static_cast<int>(x2 * scaleX);
                    y2 = static_cast<int>(y2 * scaleY);
                    std::smatch word_match;
					if (std::regex_match(word_str, word_match, search_regex)) {
                        // word found
						printf("Match found at (%d, %d) to (%d, %d)\n", x1, y1, x2, y2);
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

    PyErr_SetString(PyExc_TypeError, "Second argument must be a chivel.Image or a string");
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
        PyErr_SetString(PyExc_TypeError, "First argument must be a chivel.Image object");
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

static PyObject* chivel_mouse_down(PyObject* self, PyObject* args, PyObject* kwds) {
    int button = 0;
    static const char* kwlist[] = { "button", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i", (char**)kwlist, &button))
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

static PyObject* chivel_mouse_up(PyObject* self, PyObject* args, PyObject* kwds) {
    int button = 0;
    static const char* kwlist[] = { "button", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i", (char**)kwlist, &button))
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

static PyObject* chivel_key_click(PyObject* self, PyObject* args, PyObject* kwds) {
    int key = 0;
    int count = 1;
    static const char* kwlist[] = { "key", "count", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i|i", (char**)kwlist, &key, &count))
        return nullptr;

    if (key < 1 || key > 0xFF) {
        PyErr_SetString(PyExc_ValueError, "Key must be a valid virtual-key code (1-255)");
        return nullptr;
    }
    if (count < 1) {
        PyErr_SetString(PyExc_ValueError, "Count must be >= 1");
        return nullptr;
    }

    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = key;
    inputs[0].ki.dwFlags = 0; // key down

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = key;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP; // key up

    for (int i = 0; i < count; ++i) {
        if (SendInput(1, &inputs[0], sizeof(INPUT)) != 1) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to send key down event");
            return nullptr;
        }
        if (SendInput(1, &inputs[1], sizeof(INPUT)) != 1) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to send key up event");
            return nullptr;
        }
    }

    Py_RETURN_NONE;
}

static PyObject* chivel_key_down(PyObject* self, PyObject* args) {
    int key = 0;
    if (!PyArg_ParseTuple(args, "i", &key))
        return nullptr;

    if (key < 1 || key > 0xFF) {
        PyErr_SetString(PyExc_ValueError, "Key must be a valid virtual-key code (1-255)");
        return nullptr;
    }

    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    input.ki.dwFlags = 0; // key down

    if (SendInput(1, &input, sizeof(INPUT)) != 1) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to send key down event");
        return nullptr;
    }

    Py_RETURN_NONE;
}

static PyObject* chivel_key_up(PyObject* self, PyObject* args) {
    int key = 0;
    if (!PyArg_ParseTuple(args, "i", &key))
        return nullptr;

    if (key < 1 || key > 0xFF) {
        PyErr_SetString(PyExc_ValueError, "Key must be a valid virtual-key code (1-255)");
        return nullptr;
    }

    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    input.ki.dwFlags = KEYEVENTF_KEYUP; // key up

    if (SendInput(1, &input, sizeof(INPUT)) != 1) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to send key up event");
        return nullptr;
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

static PyObject* chivel_record(PyObject* self, PyObject* args, PyObject* kwds) {
    const char* output_path = "recording.py";
    int simplify = SIMPLIFY_ALL;
    int stop_key = VK_F12; // Default to F12
    static const char* kwlist[] = { "output_path", "simplify", "stop_key", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|sip", (char**)kwlist, &output_path, &simplify, &stop_key))
        return nullptr;

    // Import chivel._recorder and call record()
    PyObject* recorder_mod = PyImport_ImportModule("chivel._recorder");
    if (!recorder_mod) {
        PyErr_SetString(PyExc_ImportError, "Could not import chivel._recorder. Make sure it is installed.");
        return nullptr;
    }
    PyObject* record_func = PyObject_GetAttrString(recorder_mod, "record");
    Py_DECREF(recorder_mod);
    if (!record_func || !PyCallable_Check(record_func)) {
        Py_XDECREF(record_func);
        PyErr_SetString(PyExc_AttributeError, "chivel._recorder.record not found or not callable.");
        return nullptr;
    }
    PyObject* py_output_path = PyUnicode_FromString(output_path);
    PyObject* py_simplify = PyLong_FromLong(simplify);
    PyObject* py_stop_key = PyLong_FromLong(stop_key);
    PyObject* result = PyObject_CallFunctionObjArgs(record_func, py_output_path, py_simplify, py_stop_key, NULL);
    Py_DECREF(record_func);
    Py_DECREF(py_output_path);
    Py_DECREF(py_simplify);
    Py_DECREF(py_stop_key);
    if (!result)
        return nullptr;
    Py_DECREF(result);
    Py_RETURN_NONE;
}

static PyObject* chivel_play(PyObject* self, PyObject* args) {
    const char* script_path = nullptr;
    const char* play_func_name = "play";
    if (!PyArg_ParseTuple(args, "s|s", &script_path, &play_func_name)) {
        return nullptr;
    }

    bool ok = chivel::run_python_play_function(script_path, play_func_name);
    if (!ok) {
        PyErr_SetString(PyExc_RuntimeError, std::format("Failed to run {}() in the given script.", play_func_name).c_str());
        return nullptr;
    }

    Py_RETURN_NONE;
}

static PyObject* chivel_get_location(PyObject* self, PyObject* args) {
    // Get mouse position in screen coordinates
    POINT pt;
    if (!GetCursorPos(&pt)) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to get mouse position");
        return nullptr;
    }

    // Find which monitor contains the point
    struct MonitorSearch {
        POINT pt;
        int index;
        int found;
        int x, y;
    } search = { pt, 0, -1, 0, 0 };

    EnumDisplayMonitors(nullptr, nullptr, chivel::monitor_enum_proc, reinterpret_cast<LPARAM>(&search));

    if (search.found == -1) {
        // Not found, return -1 and absolute position
        return Py_BuildValue("(iii)", -1, pt.x, pt.y);
    }
    else {
        // Return monitor index and position relative to that monitor
        return Py_BuildValue("(iii)", search.found, search.x, search.y);
    }
}

static PyObject* chivel_mouse_get_display(PyObject* self, PyObject* args) {
    // Get mouse position in screen coordinates
    POINT pt;
    if (!GetCursorPos(&pt)) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to get mouse position");
        return nullptr;
    }

    // Find which monitor contains the point
    struct MonitorSearch {
        POINT pt;
        int index;
        int found;
        int x, y;
    } search = { pt, 0, -1, 0, 0 };

    EnumDisplayMonitors(nullptr, nullptr, chivel::monitor_enum_proc, reinterpret_cast<LPARAM>(&search));

    return PyLong_FromLong(search.found);
}

static PyObject* chivel_display_get_rect(PyObject* self, PyObject* args) {
    int monitor_index = 0;
    if (!PyArg_ParseTuple(args, "i", &monitor_index))
        return nullptr;

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

    // dmPosition is relative to the primary display
    int x = dm.dmPosition.x;
    int y = dm.dmPosition.y;
    int width = dm.dmPelsWidth;
    int height = dm.dmPelsHeight;

    return Py_BuildValue("(iiii)", x, y, width, height);
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

	// Text search levels
    PyModule_AddIntConstant(module, "TEXT_BLOCK", tesseract::RIL_BLOCK);
    PyModule_AddIntConstant(module, "TEXT_PARAGRAPH", tesseract::RIL_PARA);
    PyModule_AddIntConstant(module, "TEXT_LINE", tesseract::RIL_TEXTLINE);
    PyModule_AddIntConstant(module, "TEXT_WORD", tesseract::RIL_WORD);
    PyModule_AddIntConstant(module, "TEXT_SYMBOL", tesseract::RIL_SYMBOL);

	// Display count
    PyModule_AddIntConstant(module, "DISPLAY_COUNT", chivel::get_display_count());

    // Keys
    PyModule_AddIntConstant(module, "KEY_BACKSPACE", VK_BACK);
    PyModule_AddIntConstant(module, "KEY_TAB", VK_TAB);
    PyModule_AddIntConstant(module, "KEY_ENTER", VK_RETURN);
    PyModule_AddIntConstant(module, "KEY_SHIFT", VK_SHIFT);
    PyModule_AddIntConstant(module, "KEY_CTRL", VK_CONTROL);
    PyModule_AddIntConstant(module, "KEY_ALT", VK_MENU);
    PyModule_AddIntConstant(module, "KEY_PAUSE", VK_PAUSE);
    PyModule_AddIntConstant(module, "KEY_CAPSLOCK", VK_CAPITAL);
    PyModule_AddIntConstant(module, "KEY_ESC", VK_ESCAPE);
    PyModule_AddIntConstant(module, "KEY_SPACE", VK_SPACE);
    PyModule_AddIntConstant(module, "KEY_PAGEUP", VK_PRIOR);
    PyModule_AddIntConstant(module, "KEY_PAGEDOWN", VK_NEXT);
    PyModule_AddIntConstant(module, "KEY_END", VK_END);
    PyModule_AddIntConstant(module, "KEY_HOME", VK_HOME);
    PyModule_AddIntConstant(module, "KEY_LEFT", VK_LEFT);
    PyModule_AddIntConstant(module, "KEY_UP", VK_UP);
    PyModule_AddIntConstant(module, "KEY_RIGHT", VK_RIGHT);
    PyModule_AddIntConstant(module, "KEY_DOWN", VK_DOWN);
    PyModule_AddIntConstant(module, "KEY_PRINTSCREEN", VK_SNAPSHOT);
    PyModule_AddIntConstant(module, "KEY_INSERT", VK_INSERT);
    PyModule_AddIntConstant(module, "KEY_DELETE", VK_DELETE);
    PyModule_AddIntConstant(module, "KEY_NUMLOCK", VK_NUMLOCK);
    PyModule_AddIntConstant(module, "KEY_SCROLLLOCK", VK_SCROLL);
    PyModule_AddIntConstant(module, "KEY_NUMPAD0", VK_NUMPAD0);
    PyModule_AddIntConstant(module, "KEY_NUMPAD1", VK_NUMPAD1);
    PyModule_AddIntConstant(module, "KEY_NUMPAD2", VK_NUMPAD2);
    PyModule_AddIntConstant(module, "KEY_NUMPAD3", VK_NUMPAD3);
    PyModule_AddIntConstant(module, "KEY_NUMPAD4", VK_NUMPAD4);
    PyModule_AddIntConstant(module, "KEY_NUMPAD5", VK_NUMPAD5);
    PyModule_AddIntConstant(module, "KEY_NUMPAD6", VK_NUMPAD6);
    PyModule_AddIntConstant(module, "KEY_NUMPAD7", VK_NUMPAD7);
    PyModule_AddIntConstant(module, "KEY_NUMPAD8", VK_NUMPAD8);
    PyModule_AddIntConstant(module, "KEY_NUMPAD9", VK_NUMPAD9);
    PyModule_AddIntConstant(module, "KEY_MULTIPLY", VK_MULTIPLY);
    PyModule_AddIntConstant(module, "KEY_ADD", VK_ADD);
    PyModule_AddIntConstant(module, "KEY_SEPARATOR", VK_SEPARATOR);
    PyModule_AddIntConstant(module, "KEY_SUBTRACT", VK_SUBTRACT);
    PyModule_AddIntConstant(module, "KEY_DECIMAL", VK_DECIMAL);
    PyModule_AddIntConstant(module, "KEY_DIVIDE", VK_DIVIDE);
    PyModule_AddIntConstant(module, "KEY_0", 0x30);
    PyModule_AddIntConstant(module, "KEY_1", 0x31);
    PyModule_AddIntConstant(module, "KEY_2", 0x32);
    PyModule_AddIntConstant(module, "KEY_3", 0x33);
    PyModule_AddIntConstant(module, "KEY_4", 0x34);
    PyModule_AddIntConstant(module, "KEY_5", 0x35);
    PyModule_AddIntConstant(module, "KEY_6", 0x36);
    PyModule_AddIntConstant(module, "KEY_7", 0x37);
    PyModule_AddIntConstant(module, "KEY_8", 0x38);
    PyModule_AddIntConstant(module, "KEY_9", 0x39);
    PyModule_AddIntConstant(module, "KEY_A", 0x41);
    PyModule_AddIntConstant(module, "KEY_B", 0x42);
    PyModule_AddIntConstant(module, "KEY_C", 0x43);
    PyModule_AddIntConstant(module, "KEY_D", 0x44);
    PyModule_AddIntConstant(module, "KEY_E", 0x45);
    PyModule_AddIntConstant(module, "KEY_F", 0x46);
    PyModule_AddIntConstant(module, "KEY_G", 0x47);
    PyModule_AddIntConstant(module, "KEY_H", 0x48);
    PyModule_AddIntConstant(module, "KEY_I", 0x49);
    PyModule_AddIntConstant(module, "KEY_J", 0x4A);
    PyModule_AddIntConstant(module, "KEY_K", 0x4B);
    PyModule_AddIntConstant(module, "KEY_L", 0x4C);
    PyModule_AddIntConstant(module, "KEY_M", 0x4D);
    PyModule_AddIntConstant(module, "KEY_N", 0x4E);
    PyModule_AddIntConstant(module, "KEY_O", 0x4F);
    PyModule_AddIntConstant(module, "KEY_P", 0x50);
    PyModule_AddIntConstant(module, "KEY_Q", 0x51);
    PyModule_AddIntConstant(module, "KEY_R", 0x52);
    PyModule_AddIntConstant(module, "KEY_S", 0x53);
    PyModule_AddIntConstant(module, "KEY_T", 0x54);
    PyModule_AddIntConstant(module, "KEY_U", 0x55);
    PyModule_AddIntConstant(module, "KEY_V", 0x56);
    PyModule_AddIntConstant(module, "KEY_W", 0x57);
    PyModule_AddIntConstant(module, "KEY_X", 0x58);
    PyModule_AddIntConstant(module, "KEY_Y", 0x59);
    PyModule_AddIntConstant(module, "KEY_Z", 0x5A);
    PyModule_AddIntConstant(module, "KEY_F1", VK_F1);
    PyModule_AddIntConstant(module, "KEY_F2", VK_F2);
    PyModule_AddIntConstant(module, "KEY_F3", VK_F3);
    PyModule_AddIntConstant(module, "KEY_F4", VK_F4);
    PyModule_AddIntConstant(module, "KEY_F5", VK_F5);
    PyModule_AddIntConstant(module, "KEY_F6", VK_F6);
    PyModule_AddIntConstant(module, "KEY_F7", VK_F7);
    PyModule_AddIntConstant(module, "KEY_F8", VK_F8);
    PyModule_AddIntConstant(module, "KEY_F9", VK_F9);
    PyModule_AddIntConstant(module, "KEY_F10", VK_F10);
    PyModule_AddIntConstant(module, "KEY_F11", VK_F11);
    PyModule_AddIntConstant(module, "KEY_F12", VK_F12);

	PyModule_AddIntConstant(module, "SIMPLIFY_NONE", SIMPLIFY_NONE);
	PyModule_AddIntConstant(module, "SIMPLIFY_MOVE", SIMPLIFY_MOVE);
	PyModule_AddIntConstant(module, "SIMPLIFY_CLICK", SIMPLIFY_CLICK);
	PyModule_AddIntConstant(module, "SIMPLIFY_TYPE", SIMPLIFY_TYPE);
	PyModule_AddIntConstant(module, "SIMPLIFY_TIME", SIMPLIFY_TIME);
	PyModule_AddIntConstant(module, "SIMPLIFY_ALL", SIMPLIFY_ALL);

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
    {"capture", (PyCFunction)chivel_capture, METH_VARARGS | METH_KEYWORDS, "Capture the screen or a specific rectangle"},
	{"find", (PyCFunction)chivel_find, METH_VARARGS | METH_KEYWORDS, "Find rectangles or text in an image"},
	{"draw", (PyCFunction)chivel_draw, METH_VARARGS | METH_KEYWORDS, "Draw rectangle(s) on an image"},
	{"wait", chivel_wait, METH_VARARGS, "Wait for a specified number of seconds"},
	{"mouse_move", chivel_mouse_move, METH_VARARGS, "Move the mouse cursor to a position on a specific display"},
	{"mouse_click", (PyCFunction)chivel_mouse_click, METH_VARARGS | METH_KEYWORDS, "Click the mouse button"},
	{"mouse_down", (PyCFunction)chivel_mouse_down, METH_VARARGS | METH_KEYWORDS, "Press a mouse button down"},
	{"mouse_up", (PyCFunction)chivel_mouse_up, METH_VARARGS | METH_KEYWORDS, "Release a mouse button"},
	{"mouse_scroll", (PyCFunction)chivel_mouse_scroll, METH_VARARGS | METH_KEYWORDS, "Scroll the mouse wheel vertically and/or horizontally"},
    {"mouse_get_location", chivel_get_location, METH_NOARGS, "Get the current mouse display index and cursor location"},
	{"mouse_get_display", chivel_mouse_get_display, METH_NOARGS, "Get the current mouse display index"},
	{"type", (PyCFunction)chivel_type, METH_VARARGS | METH_KEYWORDS, "Type a string using the keyboard"},
	{"key_click", (PyCFunction)chivel_key_click, METH_VARARGS | METH_KEYWORDS, "Click a key on the keyboard"},
	{"key_down", chivel_key_down, METH_VARARGS, "Press a key down"},
	{"key_up", chivel_key_up, METH_VARARGS, "Release a key"},
	{"record", (PyCFunction)chivel_record, METH_VARARGS | METH_KEYWORDS, "Record a sequence of actions to a Python script"},
	{"play", chivel_play, METH_VARARGS, "Play a recorded sequence of actions from a Python script"},
	{"display_get_rect", chivel_display_get_rect, METH_VARARGS, "Get the rectangle of a specific display, relative to the primary display"},
    {nullptr, nullptr, 0, nullptr}
};

// Module definition
static struct PyModuleDef chivelmodule = {
    PyModuleDef_HEAD_INIT,
    "chivel",   // Module name
    nullptr,     // Module documentation
    0,          // Size of per-interpreter state of the module
    chivelMethods,
    chivel_slots,
    nullptr,
    nullptr,
	chivel_module_free
};

// Module initialization function
PyMODINIT_FUNC PyInit_chivel(void) {
    //return PyModule_Create(&chivelmodule);
	return PyModuleDef_Init(&chivelmodule);
}
