// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <Python.h>
#include <Windows.h>
#include <ShellScalingAPI.h> // For GetDpiForMonitor
#include <structmember.h>
#pragma comment(lib, "Shcore.lib")
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

enum MouseButton
{
	BUTTON_LEFT = 0,
	BUTTON_RIGHT = 1,
	BUTTON_MIDDLE = 2,
	BUTTON_X1 = 3, // Button 4
	BUTTON_X2 = 4  // Button 5
};

enum FlipMode
{
	FLIP_NONE = 0,
	FLIP_HORIZONTAL = 1,
	FLIP_VERTICAL = 2,
	FLIP_BOTH = FLIP_HORIZONTAL | FLIP_VERTICAL
};

enum ColorSpace
{
	COLOR_SPACE_UNKNOWN = 0,
	COLOR_SPACE_BGR = 1, // Default OpenCV color space
	COLOR_SPACE_BGRA = 2, // OpenCV with alpha channel
	COLOR_SPACE_RGB = 3, // RGB color space
	COLOR_SPACE_RGBA = 4, // RGB with alpha channel
	COLOR_SPACE_GRAY = 5, // Grayscale
	COLOR_SPACE_HSV = 6, // HSV color space

	COLOR_SPACE_DEFAULT = COLOR_SPACE_BGR // Default color space for reading images
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

	// adjusts an image for text reading
	cv::Mat adjustImage(const cv::Mat& original)
	{
		cv::Mat gray;
		if (original.channels() == 3) {
			cv::cvtColor(original, gray, cv::COLOR_BGR2GRAY);
		}
		else if (original.channels() == 1) {
			gray = original;
		}
		else {
			return cv::Mat();
		}

		// Scale up for better OCR
		cv::Mat scaled;
		const double scale = 2.0; // 2x is usually enough for screenshots
		cv::resize(gray, scaled, cv::Size(), scale, scale, cv::INTER_CUBIC);

		//// Optional: slight median blur to reduce noise
		//cv::medianBlur(scaled, scaled, 3);

		// Otsu's thresholding (works well for screenshots)
		cv::Mat binary;
		cv::threshold(scaled, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

		// Optional: mild sharpening
		cv::Mat sharpened;
		cv::GaussianBlur(binary, sharpened, cv::Size(0, 0), 1.0);
		cv::addWeighted(binary, 1.3, sharpened, -0.3, 0, sharpened);

		cv::Mat final = sharpened;
		return final;
	}

	cv::Mat readImage(char const* const path, ColorSpace color_space)
	{
		switch (color_space)
		{
		case COLOR_SPACE_BGRA:
		case COLOR_SPACE_RGBA:
			return cv::imread(path, cv::IMREAD_UNCHANGED);
		case COLOR_SPACE_GRAY:
			return cv::imread(path, cv::IMREAD_GRAYSCALE);
		default:
			return cv::imread(path, cv::IMREAD_COLOR);
		}
	}

	cv::Mat convertColorSpace(cv::Mat const& mat, ColorSpace current, ColorSpace color_space)
	{
		if (current == color_space) {
			return mat; // No conversion needed
		}
		cv::Mat converted;
		switch (color_space) {
		case COLOR_SPACE_BGR:
			if (current == COLOR_SPACE_GRAY) {
				cv::cvtColor(mat, converted, cv::COLOR_GRAY2BGR);
			}
			else if (current == COLOR_SPACE_RGBA) {
				cv::cvtColor(mat, converted, cv::COLOR_RGBA2BGR);
			}
			else if (current == COLOR_SPACE_BGRA) {
				cv::cvtColor(mat, converted, cv::COLOR_BGRA2BGR);
			}
			else if (current == COLOR_SPACE_RGB) {
				cv::cvtColor(mat, converted, cv::COLOR_RGB2BGR);
			}
			else {
				converted = mat.clone(); // No conversion needed
			}
			break;
		case COLOR_SPACE_BGRA:
			if (current == COLOR_SPACE_BGR) {
				cv::cvtColor(mat, converted, cv::COLOR_BGR2BGRA);
			}
			else if (current == COLOR_SPACE_RGBA) {
				cv::cvtColor(mat, converted, cv::COLOR_RGBA2BGRA);
			}
			else if (current == COLOR_SPACE_RGB) {
				cv::cvtColor(mat, converted, cv::COLOR_RGB2BGRA);
			}
			else if (current == COLOR_SPACE_GRAY) {
				cv::cvtColor(mat, converted, cv::COLOR_GRAY2BGRA);
			}
			else {
				converted = mat.clone(); // No conversion needed
			}
			break;
		case COLOR_SPACE_RGB:
			if (current == COLOR_SPACE_BGR) {
				cv::cvtColor(mat, converted, cv::COLOR_BGR2RGB);
			}
			else if (current == COLOR_SPACE_BGRA) {
				cv::cvtColor(mat, converted, cv::COLOR_BGRA2RGB);
			}
			else if (current == COLOR_SPACE_RGBA) {
				cv::cvtColor(mat, converted, cv::COLOR_RGBA2RGB);
			}
			else if (current == COLOR_SPACE_GRAY) {
				cv::cvtColor(mat, converted, cv::COLOR_GRAY2RGB);
			}
			else {
				converted = mat.clone(); // No conversion needed
			}
			break;
		case COLOR_SPACE_RGBA:
			if (current == COLOR_SPACE_BGR) {
				cv::cvtColor(mat, converted, cv::COLOR_BGR2RGBA);
			}
			else if (current == COLOR_SPACE_BGRA) {
				cv::cvtColor(mat, converted, cv::COLOR_BGRA2RGBA);
			}
			else if (current == COLOR_SPACE_RGB) {
				cv::cvtColor(mat, converted, cv::COLOR_RGB2RGBA);
			}
			else if (current == COLOR_SPACE_GRAY) {
				cv::cvtColor(mat, converted, cv::COLOR_GRAY2RGBA);
			}
			else {
				converted = mat.clone(); // No conversion needed
			}
			break;
		case COLOR_SPACE_GRAY:
			if (current == COLOR_SPACE_BGR) {
				cv::cvtColor(mat, converted, cv::COLOR_BGR2GRAY);
			}
			else if (current == COLOR_SPACE_BGRA) {
				cv::cvtColor(mat, converted, cv::COLOR_BGRA2GRAY);
			}
			else if (current == COLOR_SPACE_RGB) {
				cv::cvtColor(mat, converted, cv::COLOR_RGB2GRAY);
			}
			else if (current == COLOR_SPACE_RGBA) {
				cv::cvtColor(mat, converted, cv::COLOR_RGBA2GRAY);
			}
			else {
				converted = mat.clone(); // No conversion needed
			}
			break;
		case COLOR_SPACE_HSV:
			if (current == COLOR_SPACE_BGR) {
				cv::cvtColor(mat, converted, cv::COLOR_BGR2HSV);
			}
			else if (current == COLOR_SPACE_RGB) {
				cv::cvtColor(mat, converted, cv::COLOR_RGB2HSV);
			}
			else if (current == COLOR_SPACE_BGRA) {
				cv::cvtColor(mat, converted, cv::COLOR_BGRA2BGR);
				cv::cvtColor(converted, converted, cv::COLOR_BGR2HSV);
			}
			else if (current == COLOR_SPACE_RGBA) {
				cv::cvtColor(mat, converted, cv::COLOR_RGBA2BGR);
				cv::cvtColor(converted, converted, cv::COLOR_BGR2HSV);
			}
			else if (current == COLOR_SPACE_GRAY) {
				cv::cvtColor(mat, converted, cv::COLOR_GRAY2BGR);
				cv::cvtColor(converted, converted, cv::COLOR_BGR2HSV);
			}
			else {
				converted = mat.clone(); // No conversion needed
			}
			break;
		case COLOR_SPACE_UNKNOWN:
		default:
			converted = cv::Mat(); // failed
		}
		return converted;
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

	UINT get_display_dpi(int display_index = 0) {
		DISPLAY_DEVICE dd;
		dd.cb = sizeof(dd);
		if (!EnumDisplayDevices(NULL, display_index, &dd, 0))
			return 96; // fallback

		DEVMODE dm;
		dm.dmSize = sizeof(dm);
		if (!EnumDisplaySettings(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm))
			return 96; // fallback

		POINT pt = { dm.dmPosition.x, dm.dmPosition.y };
		HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

		UINT dpiX = 96, dpiY = 96;
		if (SUCCEEDED(GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)))
			return dpiX;
		return 96; // fallback
	}

	//double get_scaling_factor_for_monitor(HMONITOR hMonitor) {
	//    UINT dpiX = 96, dpiY = 96;
	//    if (SUCCEEDED(GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
	//        return dpiX / 96.0;
	//    }
	//    // Fallback for older Windows
	//    HDC screen = GetDC(NULL);
	//    int dpi = GetDeviceCaps(screen, LOGPIXELSX);
	//    ReleaseDC(NULL, screen);
	//    return dpi / 96.0;
	//}

	bool setCursorPosition(int x, int y) {
		// Find the monitor for (x, y)
		POINT pt = { x, y };
		HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
		return SetCursorPos(pt.x, pt.y);
	}

	POINT getCursorPosition()
	{
		POINT pt;
		GetCursorPos(&pt);
		HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
		return pt;
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

#pragma region Point

typedef struct {
	PyObject_HEAD
		int x;
	int y;
} CHIVELPointObject;

static void CHIVELPoint_dealloc(CHIVELPointObject* self) {
	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* CHIVELPoint_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
	CHIVELPointObject* self = (CHIVELPointObject*)type->tp_alloc(type, 0);
	if (self) {
		self->x = 0;
		self->y = 0;
	}
	return (PyObject*)self;
}

static int CHIVELPoint_init(CHIVELPointObject* self, PyObject* args, PyObject* kwds) {
	static const char* kwlist[] = { "x", "y", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii", (char**)kwlist, &self->x, &self->y))
		return -1;
	return 0;
}

static PyObject* CHIVELPoint_repr(CHIVELPointObject* self) {
	return PyUnicode_FromFormat("(%d, %d)", self->x, self->y);
}

static PyMemberDef CHIVELPoint_members[] = {
	{"x", T_INT, offsetof(CHIVELPointObject, x), 0, "x coordinate"},
	{"y", T_INT, offsetof(CHIVELPointObject, y), 0, "y coordinate"},
	{nullptr}
};

static PyTypeObject CHIVELPointType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"chivel.Point",
	sizeof(CHIVELPointObject),
	0,
	(destructor)CHIVELPoint_dealloc,
	0,
	0,
	0,
	0,
	(reprfunc)CHIVELPoint_repr,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	"Chivel Point objects",
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	CHIVELPoint_members,
	0,
	0,
	0,
	0,
	0,
	0,
	(initproc)CHIVELPoint_init,
	0,
	CHIVELPoint_new,
};

static PyObject* create_point(int x, int y) {
	PyObject* point_obj = CHIVELPoint_new(&CHIVELPointType, nullptr, nullptr);
	if (!point_obj)
		return nullptr;
	CHIVELPointObject* point = (CHIVELPointObject*)point_obj;
	point->x = x;
	point->y = y;
	return point_obj;
}

#pragma endregion

#pragma region Rect

typedef struct {
	PyObject_HEAD
		int x;
	int y;
	int width;
	int height;
} CHIVELRectObject;

static void CHIVELRect_dealloc(CHIVELRectObject* self) {
	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* CHIVELRect_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
	CHIVELRectObject* self = (CHIVELRectObject*)type->tp_alloc(type, 0);
	if (self) {
		self->x = 0;
		self->y = 0;
		self->width = 0;
		self->height = 0;
	}
	return (PyObject*)self;
}

static int CHIVELRect_init(CHIVELRectObject* self, PyObject* args, PyObject* kwds) {
	static const char* kwlist[] = { "x", "y", "width", "height", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "iiii", (char**)kwlist,
		&self->x, &self->y, &self->width, &self->height))
		return -1;
	return 0;
}

static PyObject* CHIVELRect_repr(CHIVELRectObject* self) {
	return PyUnicode_FromFormat("(%d, %d, %d, %d)", self->x, self->y, self->width, self->height);
}

// Returns a chivel.Point (x, y)
static PyObject* CHIVELRect_get_position(CHIVELRectObject* self, PyObject* /*unused*/) {
	return create_point(self->x, self->y);
}

// Returns a chivel.Point (width, height)
static PyObject* CHIVELRect_get_size(CHIVELRectObject* self, PyObject* /*unused*/) {
	return create_point(self->width, self->height);
}

static PyMethodDef CHIVELRect_methods[] = {
	{"get_position", (PyCFunction)CHIVELRect_get_position, METH_NOARGS, "Return (x, y) as a chivel.Point"},
	{"get_size", (PyCFunction)CHIVELRect_get_size, METH_NOARGS, "Return (width, height) as a chivel.Point"},
	{nullptr, nullptr, 0, nullptr}
};

static PyMemberDef CHIVELRect_members[] = {
	{"x", T_INT, offsetof(CHIVELRectObject, x), 0, "x coordinate"},
	{"y", T_INT, offsetof(CHIVELRectObject, y), 0, "y coordinate"},
	{"width", T_INT, offsetof(CHIVELRectObject, width), 0, "width"},
	{"height", T_INT, offsetof(CHIVELRectObject, height), 0, "height"},
	{nullptr}
};

static PyTypeObject CHIVELRectType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"chivel.Rect",
	sizeof(CHIVELRectObject),
	0,
	(destructor)CHIVELRect_dealloc,
	0,
	0,
	0,
	0,
	(reprfunc)CHIVELRect_repr,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	"Chivel Rect objects",
	0,
	0,
	0,
	0,
	0,
	0,
	CHIVELRect_methods,
	CHIVELRect_members,
	0,
	0,
	0,
	0,
	0,
	0,
	(initproc)CHIVELRect_init,
	0,
	CHIVELRect_new,
};

static PyObject* create_rect(int x, int y, int width, int height) {
	PyObject* rect_obj = CHIVELRect_new(&CHIVELRectType, nullptr, nullptr);
	if (!rect_obj)
		return nullptr;
	CHIVELRectObject* rect = (CHIVELRectObject*)rect_obj;
	rect->x = x;
	rect->y = y;
	rect->width = width;
	rect->height = height;
	return rect_obj;
}

#pragma endregion

#pragma region Match

typedef struct {
	PyObject_HEAD
		PyObject* rect;   // CHIVELRectObject*
	PyObject* label;  // PyUnicode or Py_None
} CHIVELMatchObject;

static void CHIVELMatch_dealloc(CHIVELMatchObject* self) {
	Py_XDECREF(self->rect);
	Py_XDECREF(self->label);
	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* CHIVELMatch_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
	CHIVELMatchObject* self = (CHIVELMatchObject*)type->tp_alloc(type, 0);
	if (self) {
		self->rect = Py_None;
		Py_INCREF(Py_None);
		self->label = Py_None;
		Py_INCREF(Py_None);
	}
	return (PyObject*)self;
}

static int CHIVELMatch_init(CHIVELMatchObject* self, PyObject* args, PyObject* kwds) {
	PyObject* rect = NULL;
	PyObject* label = Py_None;
	static const char* kwlist[] = { "rect", "label", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|O", (char**)kwlist, &rect, &label))
		return -1;
	if (!PyObject_TypeCheck(rect, &CHIVELRectType)) {
		PyErr_SetString(PyExc_TypeError, "rect must be a chivel.Rect");
		return -1;
	}
	Py_INCREF(rect);
	Py_XDECREF(self->rect);
	self->rect = rect;
	Py_XDECREF(self->label);
	if (label == NULL) label = Py_None;
	Py_INCREF(label);
	self->label = label;
	return 0;
}

static PyObject* CHIVELMatch_repr(CHIVELMatchObject* self) {
	PyObject* rect_repr = NULL;
	PyObject* label_repr = NULL;
	PyObject* result = NULL;

	// Get repr for rect
	if (self->rect && PyObject_HasAttrString(self->rect, "__repr__")) {
		rect_repr = PyObject_Repr(self->rect);
	}
	else {
		rect_repr = PyUnicode_FromString("None");
	}

	// If label is None, just return "(<rect_repr>)"
	if (!self->label || self->label == Py_None) {
		if (rect_repr) {
			result = PyUnicode_FromFormat("(%U)", rect_repr);
		}
		Py_XDECREF(rect_repr);
		return result;
	}

	// Get repr for label
	label_repr = PyObject_Repr(self->label);

	if (rect_repr && label_repr) {
		result = PyUnicode_FromFormat("(%U, \"%U\")", rect_repr, label_repr);
	}

	Py_XDECREF(rect_repr);
	Py_XDECREF(label_repr);
	return result;
}

static PyMemberDef CHIVELMatch_members[] = {
	{"rect", T_OBJECT_EX, offsetof(CHIVELMatchObject, rect), 0, "rect (chivel.Rect)"},
	{"label", T_OBJECT, offsetof(CHIVELMatchObject, label), 0, "label (str or None)"},
	{nullptr}
};

static PyTypeObject CHIVELMatchType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"chivel.Match",
	sizeof(CHIVELMatchObject),
	0,
	(destructor)CHIVELMatch_dealloc,
	0,
	0,
	0,
	0,
	(reprfunc)CHIVELMatch_repr,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	"Chivel Match objects",
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	CHIVELMatch_members,
	0,
	0,
	0,
	0,
	0,
	0,
	(initproc)CHIVELMatch_init,
	0,
	CHIVELMatch_new,
};

static PyObject* create_match(PyObject* rect_obj, PyObject* label_obj = Py_None) {
    if (!PyObject_TypeCheck(rect_obj, &CHIVELRectType)) {
        PyErr_SetString(PyExc_TypeError, "rect must be a chivel.Rect object");
        return nullptr;
    }
    if (label_obj == nullptr) {
        label_obj = Py_None;
    }
    PyObject* match_obj = CHIVELMatch_new(&CHIVELMatchType, nullptr, nullptr);
    if (!match_obj)
        return nullptr;
    CHIVELMatchObject* match = (CHIVELMatchObject*)match_obj;
    Py_XDECREF(match->rect);
    Py_INCREF(rect_obj);
    match->rect = rect_obj;
    Py_XDECREF(match->label);
    Py_INCREF(label_obj);
    match->label = label_obj;
    return match_obj;
}

#pragma endregion

#pragma region Color

typedef struct {
	PyObject_HEAD
		int r;
	int g;
	int b;
	int a;
} CHIVELColorObject;

static void CHIVELColor_dealloc(CHIVELColorObject* self) {
	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* CHIVELColor_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
	CHIVELColorObject* self = (CHIVELColorObject*)type->tp_alloc(type, 0);
	if (self) {
		self->r = 0;
		self->g = 0;
		self->b = 0;
		self->a = 255;
	}
	return (PyObject*)self;
}

static int CHIVELColor_init(CHIVELColorObject* self, PyObject* args, PyObject* kwds) {
	static const char* kwlist[] = { "r", "g", "b", "a", nullptr };
	self->r = 0;
	self->g = 0;
	self->b = 0;
	self->a = 255;
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "iii|i", (char**)kwlist, &self->r, &self->g, &self->b, &self->a))
		return -1;
	return 0;
}

static PyObject* CHIVELColor_repr(CHIVELColorObject* self) {
	return PyUnicode_FromFormat("(%d, %d, %d, %d)", self->r, self->g, self->b, self->a);
}

static PyMemberDef CHIVELColor_members[] = {
	{"r", T_INT, offsetof(CHIVELColorObject, r), 0, "red component"},
	{"g", T_INT, offsetof(CHIVELColorObject, g), 0, "green component"},
	{"b", T_INT, offsetof(CHIVELColorObject, b), 0, "blue component"},
	{"a", T_INT, offsetof(CHIVELColorObject, a), 0, "alpha component"},
	{nullptr}
};

static PyTypeObject CHIVELColorType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"chivel.Color",
	sizeof(CHIVELColorObject),
	0,
	(destructor)CHIVELColor_dealloc,
	0,
	0,
	0,
	0,
	(reprfunc)CHIVELColor_repr,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	"Chivel Color objects",
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	CHIVELColor_members,
	0,
	0,
	0,
	0,
	0,
	0,
	(initproc)CHIVELColor_init,
	0,
	CHIVELColor_new,
};

#pragma endregion

#pragma region Image

typedef struct {
	PyObject_HEAD
		cv::Mat* mat;
	ColorSpace color_space;
} CHIVELImageObject;

static void CHIVELImage_dealloc(CHIVELImageObject* self) {
	delete self->mat;
	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* CHIVELImage_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
	CHIVELImageObject* self = (CHIVELImageObject*)type->tp_alloc(type, 0);
	if (self != nullptr) {
		self->mat = new cv::Mat();
		self->color_space = COLOR_SPACE_UNKNOWN;
	}
	return (PyObject*)self;
}

static int CHIVELImage_init(CHIVELImageObject* self, PyObject* args, PyObject* kwds) {
	int width = 0, height = 0, channels = 3;
	static const char* kwlist[] = { "width", "height", "channels", nullptr };

	// Parse up to 3 optional positional or keyword arguments
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|iii", (char**)kwlist, &width, &height, &channels))
		return -1;

	// Clean up any existing mat
	if (self->mat) {
		delete self->mat;
		self->mat = nullptr;
	}

	if (width > 0 && height > 0 && channels > 0) {
		int type;
		switch (channels) {
		case 1:
			type = CV_8UC1;
			self->color_space = COLOR_SPACE_GRAY;
			break;
		case 3:
			type = CV_8UC3;
			self->color_space = COLOR_SPACE_BGR;
			break;
		case 4:
			type = CV_8UC4;
			self->color_space = COLOR_SPACE_BGRA;
			break;
		default:
			PyErr_SetString(PyExc_ValueError, "channels must be 1, 3, or 4");
			return -1;
		}
		self->mat = new cv::Mat(height, width, type, cv::Scalar(0));
	}
	else {
		// Default: empty image
		self->mat = new cv::Mat();
		self->color_space = COLOR_SPACE_UNKNOWN;
	}

	return 0;
}

// Forward declarations for CHIVELImage methods
static PyObject* CHIVELImage_get_size(CHIVELImageObject* self, PyObject* /*unused*/);
static PyObject* CHIVELImage_show(CHIVELImageObject* self, PyObject* args, PyObject* kwargs);
static PyObject* CHIVELImage_clone(CHIVELImageObject* self, PyObject* /*unused*/);
static PyObject* CHIVELImage_crop(CHIVELImageObject* self, PyObject* args);
static PyObject* CHIVELImage_grayscale(CHIVELImageObject* self, PyObject* /*unused*/);
static PyObject* CHIVELImage_scale(CHIVELImageObject* self, PyObject* args);
static PyObject* CHIVELImage_rotate(CHIVELImageObject* self, PyObject* args);
static PyObject* CHIVELImage_flip(CHIVELImageObject* self, PyObject* args);
static PyObject* CHIVELImage_resize(CHIVELImageObject* self, PyObject* args);
static PyObject* CHIVELImage_draw_rect(CHIVELImageObject* self, PyObject* args, PyObject* kwargs);
static PyObject* CHIVELImage_draw_matches(CHIVELImageObject* self, PyObject* args, PyObject* kwargs);
static PyObject* CHIVELImage_draw_line(CHIVELImageObject* self, PyObject* args, PyObject* kwargs);
static PyObject* CHIVELImage_draw_text(CHIVELImageObject* self, PyObject* args, PyObject* kwargs);
static PyObject* CHIVELImage_draw_ellipse(CHIVELImageObject* self, PyObject* args, PyObject* kwargs);
static PyObject* CHIVELImage_draw_image(CHIVELImageObject* self, PyObject* args, PyObject* kwargs);
static PyObject* CHIVELImage_invert(CHIVELImageObject* self, PyObject* /*unused*/);
static PyObject* CHIVELImage_brightness(CHIVELImageObject* self, PyObject* args);
static PyObject* CHIVELImage_contrast(CHIVELImageObject* self, PyObject* args);
static PyObject* CHIVELImage_sharpen(CHIVELImageObject* self, PyObject* args);
static PyObject* CHIVELImage_blur(CHIVELImageObject* self, PyObject* args);
static PyObject* CHIVELImage_threshold(CHIVELImageObject* self, PyObject* args);
static PyObject* CHIVELImage_normalize(CHIVELImageObject* self, PyObject* args);
static PyObject* CHIVELImage_edge(CHIVELImageObject* self, PyObject* args);
static PyObject* CHIVELImage_emboss(CHIVELImageObject* self, PyObject* /*unused*/);
static PyObject* CHIVELImage_split(CHIVELImageObject* self, PyObject* /*unused*/);
static PyObject* CHIVELImage_merge(PyObject* /*cls*/, PyObject* args);
static PyObject* CHIVELImage_convert(CHIVELImageObject* self, PyObject* args);
static PyObject* CHIVELImage_range(CHIVELImageObject* self, PyObject* args);
static PyObject* CHIVELImage_mask(CHIVELImageObject* self, PyObject* args);

static PyMethodDef CHIVELImage_methods[] = {
	{"get_size", (PyCFunction)CHIVELImage_get_size, METH_NOARGS, "Return (width, height) of the image"},
	{"show", (PyCFunction)CHIVELImage_show, METH_VARARGS | METH_KEYWORDS, "Display the image in a window"},
	{"clone", (PyCFunction)CHIVELImage_clone, METH_NOARGS, "Return a new Image object with a copy of the image data"},
	{"crop", (PyCFunction)CHIVELImage_crop, METH_VARARGS, "Crop the image to the specified rectangle (x, y, w, h)"},
	{"grayscale", (PyCFunction)CHIVELImage_grayscale, METH_NOARGS, "Convert the image to grayscale"},
	{"scale", (PyCFunction)CHIVELImage_scale, METH_VARARGS, "Scale the image by (x[, y]) factors"},
	{"rotate", (PyCFunction)CHIVELImage_rotate, METH_VARARGS, "Rotate the image by a given angle in degrees"},
	{"flip", (PyCFunction)CHIVELImage_flip, METH_VARARGS, "Flip the image"},
	{"resize", (PyCFunction)CHIVELImage_resize, METH_VARARGS, "Resize the image to the specified width and height"},
	{"draw_rect", (PyCFunction)CHIVELImage_draw_rect, METH_VARARGS | METH_KEYWORDS, "Draw a rectangle on the image"},
	{"draw_matches", (PyCFunction)CHIVELImage_draw_matches, METH_VARARGS | METH_KEYWORDS, "Draw a list of matches as rectangles on the image"},
	{"draw_line", (PyCFunction)CHIVELImage_draw_line, METH_VARARGS | METH_KEYWORDS, "Draw a line on the image"},
	{"draw_text", (PyCFunction)CHIVELImage_draw_text, METH_VARARGS | METH_KEYWORDS, "Draw text on the image"},
	{"draw_ellipse", (PyCFunction)CHIVELImage_draw_ellipse, METH_VARARGS | METH_KEYWORDS, "Draw an ellipse on the image"},
	{"draw_image", (PyCFunction)CHIVELImage_draw_image, METH_VARARGS | METH_KEYWORDS, "Draw another image onto this image at a specified position with optional alpha blending"},
	{"invert", (PyCFunction)CHIVELImage_invert, METH_NOARGS, "Invert the colors of the image"},
	{"brightness", (PyCFunction)CHIVELImage_brightness, METH_VARARGS, "Adjust the brightness of the image by a given value"},
	{"contrast", (PyCFunction)CHIVELImage_contrast, METH_VARARGS, "Adjust the contrast of the image by a given factor"},
	{"sharpen", (PyCFunction)CHIVELImage_sharpen, METH_VARARGS, "Sharpen the image by a given strength factor"},
	{"blur", (PyCFunction)CHIVELImage_blur, METH_VARARGS, "Apply Gaussian blur to the image with a specified kernel size"},
	{"threshold", (PyCFunction)CHIVELImage_threshold, METH_VARARGS, "Apply binary thresholding to the image"},
	{"normalize", (PyCFunction)CHIVELImage_normalize, METH_VARARGS, "Normalize the image pixel values"},
	{"edge", (PyCFunction)CHIVELImage_edge, METH_VARARGS, "Detect edges in the image using Canny edge detection"},
	{"emboss", (PyCFunction)CHIVELImage_emboss, METH_NOARGS, "Apply an emboss effect to the image"},
	{"split", (PyCFunction)CHIVELImage_split, METH_NOARGS, "Split the image into its color channels"},
	{"merge", (PyCFunction)CHIVELImage_merge, METH_VARARGS, "Merge multiple channel images into a single image"},
	{"convert", (PyCFunction)CHIVELImage_convert, METH_VARARGS, "Convert the image to a specified color space"},
	{"range", (PyCFunction)CHIVELImage_range, METH_VARARGS, "Check if the image is within a specified color range"},
	{"mask", (PyCFunction)CHIVELImage_mask, METH_VARARGS, "Apply a mask to the image"},
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

static PyObject* CHIVELImage_get_size(CHIVELImageObject* self, PyObject* /*unused*/) {
	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}
	int width = self->mat->cols;
	int height = self->mat->rows;

	return create_point(width, height);
}

static PyObject* CHIVELImage_show(CHIVELImageObject* self, PyObject* args, PyObject* kwargs) {
	const char* window_name = "Image";
	static const char* kwlist[] = { "window_name", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|s", (char**)kwlist, &window_name))
		return nullptr;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	cv::imshow(window_name, *(self->mat));
	cv::waitKey(0);

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_clone(CHIVELImageObject* self, PyObject* /*unused*/) {
	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	// Create a new Image object
	PyObject* new_obj = CHIVELImage_new(Py_TYPE(self), nullptr, nullptr);
	if (!new_obj)
		return nullptr;

	CHIVELImageObject* new_img = (CHIVELImageObject*)new_obj;
	delete new_img->mat;
	new_img->mat = new cv::Mat(self->mat->clone());

	return new_obj;
}

static PyObject* CHIVELImage_crop(CHIVELImageObject* self, PyObject* args) {
	PyObject* rect_obj = nullptr;
	if (!PyArg_ParseTuple(args, "O", &rect_obj))
		return nullptr;

	if (!PyObject_TypeCheck(rect_obj, &CHIVELRectType)) {
		PyErr_SetString(PyExc_TypeError, "Argument must be a chivel.Rect object");
		return nullptr;
	}
	CHIVELRectObject* rect = (CHIVELRectObject*)rect_obj;
	int x = rect->x;
	int y = rect->y;
	int w = rect->width;
	int h = rect->height;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	// Ensure the rectangle is within image bounds
	if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
		x + w > self->mat->cols || y + h > self->mat->rows) {
		PyErr_SetString(PyExc_ValueError, "Crop rectangle is out of image bounds");
		return nullptr;
	}

	// Crop in-place
	cv::Mat cropped = (*self->mat)(cv::Rect(x, y, w, h)).clone();
	self->mat->release();
	*self->mat = cropped;

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_grayscale(CHIVELImageObject* self, PyObject* /*unused*/) {
	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	if (self->mat->channels() == 1) {
		// Already grayscale, do nothing
		Py_RETURN_NONE;
	}

	cv::Mat gray;
	cv::cvtColor(*self->mat, gray, cv::COLOR_BGR2GRAY);
	self->mat->release();
	*self->mat = gray;

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_scale(CHIVELImageObject* self, PyObject* args) {
	double scale_x = 1.0;
	double scale_y = 1.0;
	int arg_count = static_cast<int>(PyTuple_Size(args));
	if (arg_count < 1 || arg_count > 2) {
		PyErr_SetString(PyExc_TypeError, "scale() takes 1 or 2 positional arguments (x[, y])");
		return nullptr;
	}
	if (!PyArg_ParseTuple(args, "d|d", &scale_x, &scale_y))
		return nullptr;
	if (arg_count == 1) {
		scale_y = scale_x;
	}

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}
	if (scale_x <= 0.0 || scale_y <= 0.0) {
		PyErr_SetString(PyExc_ValueError, "Scale factors must be positive");
		return nullptr;
	}

	int new_width = static_cast<int>(self->mat->cols * scale_x);
	int new_height = static_cast<int>(self->mat->rows * scale_y);
	if (new_width < 1 || new_height < 1) {
		PyErr_SetString(PyExc_ValueError, "Resulting image size is too small");
		return nullptr;
	}

	cv::Mat scaled;
	cv::resize(*self->mat, scaled, cv::Size(new_width, new_height), 0, 0, cv::INTER_LINEAR);
	self->mat->release();
	*self->mat = scaled;

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_rotate(CHIVELImageObject* self, PyObject* args) {
	double angle = 0.0;
	if (!PyArg_ParseTuple(args, "d", &angle))
		return nullptr;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	// Get image center
	cv::Point2f center(self->mat->cols / 2.0f, self->mat->rows / 2.0f);

	// Compute rotation matrix
	cv::Mat rot = cv::getRotationMatrix2D(center, angle, 1.0);

	// Compute bounding box of the rotated image
	double abs_cos = std::abs(rot.at<double>(0, 0));
	double abs_sin = std::abs(rot.at<double>(0, 1));
	int bound_w = int(self->mat->rows * abs_sin + self->mat->cols * abs_cos);
	int bound_h = int(self->mat->rows * abs_cos + self->mat->cols * abs_sin);

	// Adjust the rotation matrix to take into account translation
	rot.at<double>(0, 2) += bound_w / 2.0 - center.x;
	rot.at<double>(1, 2) += bound_h / 2.0 - center.y;

	cv::Mat rotated;
	cv::warpAffine(*self->mat, rotated, rot, cv::Size(bound_w, bound_h), cv::INTER_LINEAR, cv::BORDER_REPLICATE);

	self->mat->release();
	*self->mat = rotated;

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_flip(CHIVELImageObject* self, PyObject* args) {
	int flags = FLIP_NONE;
	if (!PyArg_ParseTuple(args, "i", &flags))
		return nullptr;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	int flipCode;
	if ((flags & FLIP_BOTH) == FLIP_BOTH) {
		flipCode = -1; // both
	}
	else if (flags & FLIP_HORIZONTAL) {
		flipCode = 1; // horizontal
	}
	else if (flags & FLIP_VERTICAL) {
		flipCode = 0; // vertical
	}
	else {
		PyErr_SetString(PyExc_ValueError, "At least one of FLIP_VERTICAL or FLIP_HORIZONTAL must be set");
		return nullptr;
	}

	cv::Mat flipped;
	cv::flip(*self->mat, flipped, flipCode);
	self->mat->release();
	*self->mat = flipped;

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_resize(CHIVELImageObject* self, PyObject* args) {
	PyObject* point_obj = nullptr;
	if (!PyArg_ParseTuple(args, "O", &point_obj))
		return nullptr;

	if (!PyObject_TypeCheck(point_obj, &CHIVELPointType)) {
		PyErr_SetString(PyExc_TypeError, "Argument must be a chivel.Point object");
		return nullptr;
	}

	CHIVELPointObject* point = (CHIVELPointObject*)point_obj;
	int width = point->x;
	int height = point->y;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}
	if (width < 1 || height < 1) {
		PyErr_SetString(PyExc_ValueError, "Width and height must be positive integers");
		return nullptr;
	}

	cv::Mat resized;
	cv::resize(*self->mat, resized, cv::Size(width, height), 0, 0, cv::INTER_LINEAR);
	self->mat->release();
	*self->mat = resized;

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_draw_rect(CHIVELImageObject* self, PyObject* args, PyObject* kwargs) {
	PyObject* rect_obj = nullptr;
	PyObject* color_obj = nullptr;
	int thickness = 2;
	static const char* kwlist[] = { "rect", "color", "thickness", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|Oi", (char**)kwlist, &rect_obj, &color_obj, &thickness))
		return nullptr;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	// Parse rect as chivel.Rect
	if (!PyObject_TypeCheck(rect_obj, &CHIVELRectType)) {
		PyErr_SetString(PyExc_TypeError, "rect must be a chivel.Rect object");
		return nullptr;
	}
	CHIVELRectObject* rect = (CHIVELRectObject*)rect_obj;
	int x = rect->x;
	int y = rect->y;
	int w = rect->width;
	int h = rect->height;
	if (w <= 0 || h <= 0) {
		PyErr_SetString(PyExc_ValueError, "Width and height must be positive");
		return nullptr;
	}

	// Default color: red (BGR: 0,0,255)
	cv::Scalar color(0, 0, 255);
	if (color_obj && PyObject_TypeCheck(color_obj, &CHIVELColorType)) {
		CHIVELColorObject* col = (CHIVELColorObject*)color_obj;
		color = cv::Scalar(col->b, col->g, col->r);
	}

	if (thickness < 1) {
		PyErr_SetString(PyExc_ValueError, "thickness must be >= 1");
		return nullptr;
	}

	cv::rectangle(*self->mat, cv::Rect(x, y, w, h), color, thickness);

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_draw_matches(CHIVELImageObject* self, PyObject* args, PyObject* kwargs) {
	PyObject* matches_obj = nullptr;
	PyObject* color_obj = nullptr;
	int thickness = 2;
	static const char* kwlist[] = { "rects", "color", "thickness", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|Oi", (char**)kwlist, &matches_obj, &color_obj, &thickness))
		return nullptr;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	if (!PyList_Check(matches_obj)) {
		PyErr_SetString(PyExc_TypeError, "rects must be a list of chivel.Match objects");
		return nullptr;
	}

	// Default color: red (BGR: 0,0,255)
	cv::Scalar color(0, 0, 255);
	if (color_obj && PyObject_TypeCheck(color_obj, &CHIVELColorType)) {
		CHIVELColorObject* col = (CHIVELColorObject*)color_obj;
		color = cv::Scalar(col->b, col->g, col->r);
	}

	if (thickness < 1) {
		PyErr_SetString(PyExc_ValueError, "thickness must be >= 1");
		return nullptr;
	}

	Py_ssize_t n = PyList_Size(matches_obj);
	for (Py_ssize_t i = 0; i < n; ++i) {
		PyObject* match_obj = PyList_GetItem(matches_obj, i); // Borrowed reference
		if (!PyObject_TypeCheck(match_obj, &CHIVELMatchType)) {
			PyErr_SetString(PyExc_TypeError, "Each item must be a chivel.Match object");
			return nullptr;
		}
		CHIVELMatchObject* match = (CHIVELMatchObject*)match_obj;
		if (!match->rect || !PyObject_TypeCheck(match->rect, &CHIVELRectType)) {
			PyErr_SetString(PyExc_TypeError, "Match.rect must be a chivel.Rect object");
			return nullptr;
		}
		CHIVELRectObject* rect = (CHIVELRectObject*)match->rect;
		int x = rect->x;
		int y = rect->y;
		int w = rect->width;
		int h = rect->height;
		if (w <= 0 || h <= 0) {
			continue; // Skip invalid rectangles
		}
		cv::rectangle(*self->mat, cv::Rect(x, y, w, h), color, thickness);
	}

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_draw_line(CHIVELImageObject* self, PyObject* args, PyObject* kwargs) {
	PyObject* start_obj = nullptr;
	PyObject* end_obj = nullptr;
	PyObject* color_obj = nullptr;
	int thickness = 2;
	static const char* kwlist[] = { "start", "end", "color", "thickness", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|Oi", (char**)kwlist, &start_obj, &end_obj, &color_obj, &thickness))
		return nullptr;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	// Parse start and end as chivel.Point objects
	if (!PyObject_TypeCheck(start_obj, &CHIVELPointType) || !PyObject_TypeCheck(end_obj, &CHIVELPointType)) {
		PyErr_SetString(PyExc_TypeError, "start and end must be chivel.Point objects");
		return nullptr;
	}
	CHIVELPointObject* start = (CHIVELPointObject*)start_obj;
	CHIVELPointObject* end = (CHIVELPointObject*)end_obj;
	int x1 = start->x;
	int y1 = start->y;
	int x2 = end->x;
	int y2 = end->y;

	// Default color: red (BGR: 0,0,255)
	cv::Scalar color(0, 0, 255);
	if (color_obj && PyObject_TypeCheck(color_obj, &CHIVELColorType)) {
		CHIVELColorObject* col = (CHIVELColorObject*)color_obj;
		color = cv::Scalar(col->b, col->g, col->r);
	}

	if (thickness < 1) {
		PyErr_SetString(PyExc_ValueError, "thickness must be >= 1");
		return nullptr;
	}

	cv::line(*self->mat, cv::Point(x1, y1), cv::Point(x2, y2), color, thickness);

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_draw_text(CHIVELImageObject* self, PyObject* args, PyObject* kwargs) {
	const char* text = "";
	PyObject* pos_obj = nullptr;
	PyObject* color_obj = nullptr;
	int font_size = 24;
	int thickness = -1;
	static const char* kwlist[] = { "text", "pos", "color", "font_size", "thickness", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sO|Oii", (char**)kwlist, &text, &pos_obj, &color_obj, &font_size, &thickness))
		return nullptr;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	// Parse pos as chivel.Point
	if (!PyObject_TypeCheck(pos_obj, &CHIVELPointType)) {
		PyErr_SetString(PyExc_TypeError, "pos must be a chivel.Point object");
		return nullptr;
	}
	CHIVELPointObject* point = (CHIVELPointObject*)pos_obj;
	int x = point->x;
	int y = point->y;

	if (font_size <= 0) {
		PyErr_SetString(PyExc_ValueError, "font_size must be positive");
		return nullptr;
	}

	// Default color: red (BGR: 0,0,255)
	cv::Scalar color(0, 0, 255);
	if (color_obj && PyObject_TypeCheck(color_obj, &CHIVELColorType)) {
		CHIVELColorObject* col = (CHIVELColorObject*)color_obj;
		color = cv::Scalar(col->b, col->g, col->r);
	}

	// Use OpenCV's Hershey font, scale to match font_size in pixels
	int font_face = cv::FONT_HERSHEY_SIMPLEX;
	double font_scale = font_size / 24.0; // 24 is a reasonable default pixel height for scale=1
	int use_thickness = (thickness >= 0) ? thickness : std::max(1, font_size / 12);
	if (use_thickness < 1) use_thickness = 1;

	cv::putText(*self->mat, text, cv::Point(x, y), font_face, font_scale, color, use_thickness, cv::LINE_AA);

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_draw_ellipse(CHIVELImageObject* self, PyObject* args, PyObject* kwargs) {
	PyObject* center_obj = nullptr;
	PyObject* radius_or_axes_obj = nullptr;
	PyObject* color_obj = nullptr;
	int thickness = 2;
	double angle = 0.0;
	static const char* kwlist[] = { "center", "radius_or_axes", "color", "thickness", "angle", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|Oidi", (char**)kwlist,
		&center_obj, &radius_or_axes_obj, &color_obj, &thickness, &angle))
		return nullptr;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	// Parse center as chivel.Point
	if (!PyObject_TypeCheck(center_obj, &CHIVELPointType)) {
		PyErr_SetString(PyExc_TypeError, "center must be a chivel.Point object");
		return nullptr;
	}
	CHIVELPointObject* center = (CHIVELPointObject*)center_obj;
	int cx = center->x;
	int cy = center->y;

	// Default color: red (BGR: 0,0,255)
	cv::Scalar color(0, 0, 255);
	if (color_obj && PyObject_TypeCheck(color_obj, &CHIVELColorType)) {
		CHIVELColorObject* col = (CHIVELColorObject*)color_obj;
		color = cv::Scalar(col->b, col->g, col->r);
	}

	if (PyLong_Check(radius_or_axes_obj)) {
		// Draw circle
		int radius = (int)PyLong_AsLong(radius_or_axes_obj);
		if (radius <= 0) {
			PyErr_SetString(PyExc_ValueError, "radius must be positive");
			return nullptr;
		}
		cv::circle(*self->mat, cv::Point(cx, cy), radius, color, thickness, cv::LINE_AA);
	}
	else if (PyTuple_Check(radius_or_axes_obj) && PyTuple_Size(radius_or_axes_obj) == 2) {
		// Draw ellipse
		int rx, ry;
		if (!PyArg_ParseTuple(radius_or_axes_obj, "ii", &rx, &ry)) {
			PyErr_SetString(PyExc_TypeError, "axes must be a tuple of 2 integers (rx, ry)");
			return nullptr;
		}
		if (rx <= 0 || ry <= 0) {
			PyErr_SetString(PyExc_ValueError, "axes must be positive");
			return nullptr;
		}
		cv::ellipse(*self->mat, cv::Point(cx, cy), cv::Size(rx, ry), angle, 0, 360, color, thickness, cv::LINE_AA);
	}
	else {
		PyErr_SetString(PyExc_TypeError, "radius_or_axes must be an int (radius) or tuple of 2 ints (axes)");
		return nullptr;
	}

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_draw_image(CHIVELImageObject* self, PyObject* args, PyObject* kwargs) {
	PyObject* src_obj = nullptr;
	PyObject* pos_obj = nullptr;
	double alpha = 1.0;
	static const char* kwlist[] = { "src", "pos", "alpha", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|d", (char**)kwlist, &src_obj, &pos_obj, &alpha))
		return nullptr;

	if (!PyObject_TypeCheck(src_obj, &CHIVELImageType)) {
		PyErr_SetString(PyExc_TypeError, "src must be a chivel.Image object");
		return nullptr;
	}
	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Destination image data is empty");
		return nullptr;
	}
	CHIVELImageObject* src = (CHIVELImageObject*)src_obj;
	if (!src->mat || src->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Source image data is empty");
		return nullptr;
	}
	if (!PyObject_TypeCheck(pos_obj, &CHIVELPointType)) {
		PyErr_SetString(PyExc_TypeError, "pos must be a chivel.Point object");
		return nullptr;
	}
	CHIVELPointObject* point = (CHIVELPointObject*)pos_obj;
	int x = point->x;
	int y = point->y;
	if (alpha < 0.0 || alpha > 1.0) {
		PyErr_SetString(PyExc_ValueError, "alpha must be between 0.0 and 1.0");
		return nullptr;
	}

	cv::Mat& dst = *self->mat;
	const cv::Mat& srcMat = *src->mat;

	int w = srcMat.cols;
	int h = srcMat.rows;

	// Ensure ROI is within destination bounds
	if (x < 0 || y < 0 || x + w > dst.cols || y + h > dst.rows) {
		PyErr_SetString(PyExc_ValueError, "Source image does not fit at the given position in destination image");
		return nullptr;
	}

	cv::Mat dstROI = dst(cv::Rect(x, y, w, h));

	if (srcMat.channels() == 4) {
		// Use source alpha channel for blending
		std::vector<cv::Mat> srcChannels;
		cv::split(srcMat, srcChannels);
		cv::Mat srcRGB, srcAlpha;
		cv::merge(std::vector<cv::Mat>{srcChannels[0], srcChannels[1], srcChannels[2]}, srcRGB);
		srcAlpha = srcChannels[3];

		// Normalize alpha to [0,1] and multiply by user alpha
		cv::Mat alphaMask;
		srcAlpha.convertTo(alphaMask, CV_32F, (alpha / 255.0));

		// Convert to float for blending
		cv::Mat dstROI32F, srcRGB32F, alphaMask3;
		dstROI.convertTo(dstROI32F, CV_32F);
		srcRGB.convertTo(srcRGB32F, CV_32F);

		// Expand alpha to 3 channels
		cv::Mat alphaMaskChannels[] = { alphaMask, alphaMask, alphaMask };
		cv::merge(alphaMaskChannels, 3, alphaMask3);

		// Blend: dst = src * alpha + dst * (1 - alpha)
		cv::Mat blended;
		cv::multiply(srcRGB32F, alphaMask3, srcRGB32F, 1.0 / 255.0);
		cv::multiply(dstROI32F, cv::Scalar::all(1.0), dstROI32F, 1.0 / 255.0);
		cv::multiply(dstROI32F, cv::Scalar::all(1.0) - alphaMask3 / 255.0, dstROI32F);
		blended = srcRGB32F + dstROI32F;
		blended.convertTo(dstROI, dstROI.type(), 255.0);
	}
	else {
		// No alpha channel, use uniform alpha
		if (alpha >= 1.0) {
			srcMat.copyTo(dstROI);
		}
		else if (alpha <= 0.0) {
			// Do nothing
		}
		else {
			cv::addWeighted(srcMat, alpha, dstROI, 1.0 - alpha, 0.0, dstROI);
		}
	}

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_invert(CHIVELImageObject* self, PyObject* /*unused*/) {
	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}
	cv::bitwise_not(*self->mat, *self->mat);
	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_brightness(CHIVELImageObject* self, PyObject* args) {
	double value = 0.0;
	if (!PyArg_ParseTuple(args, "d", &value))
		return nullptr;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	// Add brightness (value can be negative or positive)
	cv::Mat result;
	self->mat->convertTo(result, -1, 1, value);
	self->mat->release();
	*self->mat = result;

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_contrast(CHIVELImageObject* self, PyObject* args) {
	double factor = 1.0;
	if (!PyArg_ParseTuple(args, "d", &factor))
		return nullptr;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	// Adjust contrast (factor > 1.0 increases, 0 < factor < 1.0 decreases)
	cv::Mat result;
	self->mat->convertTo(result, -1, factor, 0);
	self->mat->release();
	*self->mat = result;

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_sharpen(CHIVELImageObject* self, PyObject* args) {
	double strength = 1.0;
	if (!PyArg_ParseTuple(args, "|d", &strength))
		return nullptr;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	// Sharpening kernel: center weight = 5, neighbors = -1 (classic)
	// Allow strength to scale the effect
	cv::Mat kernel = (cv::Mat_<float>(3, 3) <<
		0, -strength, 0,
		-strength, 1 + 4 * strength, -strength,
		0, -strength, 0);

	cv::Mat result;
	cv::filter2D(*self->mat, result, self->mat->depth(), kernel);
	self->mat->release();
	*self->mat = result;

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_blur(CHIVELImageObject* self, PyObject* args) {
	int ksize = 3;
	if (!PyArg_ParseTuple(args, "|i", &ksize))
		return nullptr;

	// scale size
	if (ksize < 1) {
		PyErr_SetString(PyExc_ValueError, "Blur amount must be a positive integer");
		return nullptr;
	}
	if (ksize == 0)
	{
		// do nothing
		Py_RETURN_NONE;
	}
	ksize = (ksize - 1) * 2 + 1;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	cv::Mat result;
	cv::GaussianBlur(*self->mat, result, cv::Size(ksize, ksize), 0);
	self->mat->release();
	*self->mat = result;

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_threshold(CHIVELImageObject* self, PyObject* args) {
	double thresh = 128.0;
	double maxval = 255.0;
	if (!PyArg_ParseTuple(args, "|dd", &thresh, &maxval))
		return nullptr;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	// Convert to grayscale if needed
	cv::Mat gray;
	if (self->mat->channels() == 1) {
		gray = *self->mat;
	}
	else {
		cv::cvtColor(*self->mat, gray, cv::COLOR_BGR2GRAY);
	}

	cv::Mat result;
	cv::threshold(gray, result, thresh, maxval, cv::THRESH_BINARY);
	self->mat->release();
	*self->mat = result;

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_normalize(CHIVELImageObject* self, PyObject* args) {
	double alpha = 0.0;
	double beta = 255.0;
	int norm_type = cv::NORM_MINMAX;
	if (!PyArg_ParseTuple(args, "|dd", &alpha, &beta))
		return nullptr;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	cv::Mat result;
	cv::normalize(*self->mat, result, alpha, beta, norm_type);
	self->mat->release();
	*self->mat = result;

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_edge(CHIVELImageObject* self, PyObject* args) {
	double threshold1 = 100.0;
	double threshold2 = 200.0;
	if (!PyArg_ParseTuple(args, "|dd", &threshold1, &threshold2))
		return nullptr;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	// Convert to grayscale if needed
	cv::Mat gray;
	if (self->mat->channels() == 1) {
		gray = *self->mat;
	}
	else {
		cv::cvtColor(*self->mat, gray, cv::COLOR_BGR2GRAY);
	}

	cv::Mat edges;
	cv::Canny(gray, edges, threshold1, threshold2);

	self->mat->release();
	*self->mat = edges;

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_emboss(CHIVELImageObject* self, PyObject* /*unused*/) {
	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	// Convert to grayscale for classic emboss effect
	cv::Mat gray;
	if (self->mat->channels() == 1) {
		gray = *self->mat;
	}
	else {
		cv::cvtColor(*self->mat, gray, cv::COLOR_BGR2GRAY);
	}

	// Emboss kernel (diagonal light)
	cv::Mat kernel = (cv::Mat_<float>(3, 3) <<
		-2, -1, 0,
		-1, 1, 1,
		0, 1, 2);

	cv::Mat embossed;
	cv::filter2D(gray, embossed, CV_8U, kernel);

	// Add 128 to shift the result to visible range (optional, but common)
	cv::add(embossed, cv::Scalar(128), embossed);

	self->mat->release();
	*self->mat = embossed;

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_split(CHIVELImageObject* self, PyObject* /*unused*/) {
	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}
	if (self->color_space == COLOR_SPACE_UNKNOWN)
	{
		PyErr_SetString(PyExc_ValueError, "Image color space is unknown");
		return nullptr;
	}

	std::vector<cv::Mat> channels;
	cv::split(*self->mat, channels);

	PyObject* pyList = PyList_New(static_cast<Py_ssize_t>(channels.size()));
	if (!pyList)
		return nullptr;

	for (size_t i = 0; i < channels.size(); ++i) {
		PyObject* img_obj = CHIVELImage_new(Py_TYPE(self), nullptr, nullptr);
		if (!img_obj) {
			Py_DECREF(pyList);
			return nullptr;
		}
		CHIVELImageObject* img = (CHIVELImageObject*)img_obj;
		delete img->mat;
		img->mat = new cv::Mat(channels[i]);
		img->color_space = COLOR_SPACE_GRAY;
		PyList_SET_ITEM(pyList, i, img_obj); // Steals reference
	}

	return pyList;
}

static PyObject* CHIVELImage_merge(PyObject* self, PyObject* args) {
	PyObject* seqObj = nullptr;
	if (!PyArg_ParseTuple(args, "O", &seqObj))
		return nullptr;

	if (!PySequence_Check(seqObj)) {
		PyErr_SetString(PyExc_TypeError, "Argument must be a sequence of chivel.Image objects");
		return nullptr;
	}

	Py_ssize_t n = PySequence_Size(seqObj);
	if (n < 1) {
		PyErr_SetString(PyExc_ValueError, "At least one channel is required");
		return nullptr;
	}

	std::vector<cv::Mat> channels;
	int rows = -1, cols = -1, type = -1;

	for (Py_ssize_t i = 0; i < n; ++i) {
		PyObject* item = PySequence_GetItem(seqObj, i);
		if (!item || !PyObject_TypeCheck(item, &CHIVELImageType)) {
			Py_XDECREF(item);
			PyErr_SetString(PyExc_TypeError, "All elements must be chivel.Image objects");
			return nullptr;
		}
		CHIVELImageObject* img = (CHIVELImageObject*)item;
		if (!img->mat || img->mat->empty()) {
			Py_DECREF(item);
			PyErr_SetString(PyExc_ValueError, "All images must be non-empty");
			return nullptr;
		}
		if (img->mat->channels() != 1) {
			Py_DECREF(item);
			PyErr_SetString(PyExc_ValueError, "Each image must be single-channel");
			return nullptr;
		}
		if (rows == -1) {
			rows = img->mat->rows;
			cols = img->mat->cols;
			type = img->mat->type();
		}
		else if (img->mat->rows != rows || img->mat->cols != cols || img->mat->type() != type) {
			Py_DECREF(item);
			PyErr_SetString(PyExc_ValueError, "All images must have the same size and type");
			return nullptr;
		}
		channels.push_back(*img->mat);
		Py_DECREF(item);
	}

	cv::Mat merged;
	cv::merge(channels, merged);

	// Modify self in-place
	CHIVELImageObject* out_img = (CHIVELImageObject*)self;
	if (out_img->mat) {
		delete out_img->mat;
	}
	out_img->mat = new cv::Mat(merged);

	// Set color_space based on number of channels
	switch (static_cast<int>(channels.size())) {
	case 1:
		out_img->color_space = COLOR_SPACE_GRAY;
		break;
	case 3:
		out_img->color_space = COLOR_SPACE_BGR;
		break;
	case 4:
		out_img->color_space = COLOR_SPACE_BGRA;
		break;
	default:
		out_img->color_space = COLOR_SPACE_UNKNOWN;
		break;
	}

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_convert(CHIVELImageObject* self, PyObject* args) {
	int target_space = COLOR_SPACE_BGR;
	static const char* kwlist[] = { "color_space", nullptr };
	if (!PyArg_ParseTuple(args, "i", (char**)kwlist, &target_space))
		return nullptr;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	ColorSpace src_space = self->color_space;
	ColorSpace dst_space = static_cast<ColorSpace>(target_space);

	// If already in the target color space, return as requested
	if (src_space == dst_space) {
		Py_RETURN_NONE;
	}

	// convert to the new color space
	cv::Mat result = chivel::convertColorSpace(*self->mat, src_space, dst_space);
	if (result.empty()) {
		PyErr_SetString(PyExc_ValueError, "Color space conversion failed or unsupported.");
		return nullptr;
	}

	self->mat->release();
	*self->mat = result;
	self->color_space = dst_space;
	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_range(CHIVELImageObject* self, PyObject* args) {
	PyObject* lower_obj = nullptr;
	PyObject* upper_obj = nullptr;
	if (!PyArg_ParseTuple(args, "OO", &lower_obj, &upper_obj))
		return nullptr;

	if (!self->mat || self->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image data is empty");
		return nullptr;
	}

	// Accept either chivel.Color or 3-tuple for lower and upper
	int l0, l1, l2, u0, u1, u2;
	if (PyObject_TypeCheck(lower_obj, &CHIVELColorType)) {
		CHIVELColorObject* col = (CHIVELColorObject*)lower_obj;
		l0 = col->b; l1 = col->g; l2 = col->r;
	}
	else if (PyTuple_Check(lower_obj) && PyTuple_Size(lower_obj) == 3) {
		if (!PyArg_ParseTuple(lower_obj, "iii", &l0, &l1, &l2)) {
			PyErr_SetString(PyExc_TypeError, "lower must be a chivel.Color or 3-tuple of ints");
			return nullptr;
		}
	}
	else {
		PyErr_SetString(PyExc_TypeError, "lower must be a chivel.Color or 3-tuple of ints");
		return nullptr;
	}

	if (PyObject_TypeCheck(upper_obj, &CHIVELColorType)) {
		CHIVELColorObject* col = (CHIVELColorObject*)upper_obj;
		u0 = col->b; u1 = col->g; u2 = col->r;
	}
	else if (PyTuple_Check(upper_obj) && PyTuple_Size(upper_obj) == 3) {
		if (!PyArg_ParseTuple(upper_obj, "iii", &u0, &u1, &u2)) {
			PyErr_SetString(PyExc_TypeError, "upper must be a chivel.Color or 3-tuple of ints");
			return nullptr;
		}
	}
	else {
		PyErr_SetString(PyExc_TypeError, "upper must be a chivel.Color or 3-tuple of ints");
		return nullptr;
	}

	cv::Mat input = *self->mat;
	cv::Mat mask;
	cv::inRange(input, cv::Scalar(l0, l1, l2), cv::Scalar(u0, u1, u2), mask);

	self->mat->release();
	*self->mat = mask;
	self->color_space = COLOR_SPACE_GRAY;

	Py_RETURN_NONE;
}

static PyObject* CHIVELImage_mask(CHIVELImageObject* self, PyObject* args) {
	PyObject* mask_obj = nullptr;
	if (!PyArg_ParseTuple(args, "O", &mask_obj))
		return nullptr;

	if (!PyObject_TypeCheck(mask_obj, &CHIVELImageType)) {
		PyErr_SetString(PyExc_TypeError, "Argument must be a chivel.Image object (mask)");
		return nullptr;
	}
	CHIVELImageObject* mask_img = (CHIVELImageObject*)mask_obj;

	if (!self->mat || self->mat->empty() || !mask_img->mat || mask_img->mat->empty()) {
		PyErr_SetString(PyExc_ValueError, "Image or mask data is empty");
		return nullptr;
	}
	if (mask_img->mat->type() != CV_8UC1) {
		PyErr_SetString(PyExc_TypeError, "Mask must be a single-channel 8-bit image");
		return nullptr;
	}
	if (self->mat->rows != mask_img->mat->rows || self->mat->cols != mask_img->mat->cols) {
		PyErr_SetString(PyExc_ValueError, "Mask size must match image size");
		return nullptr;
	}

	cv::Mat result;
	cv::bitwise_and(*self->mat, *self->mat, result, *mask_img->mat);

	CHIVELImageObject* out_img = (CHIVELImageObject*)self;
	delete out_img->mat;
	out_img->mat = new cv::Mat(result);
	out_img->color_space = self->color_space;
	Py_RETURN_NONE;
}

#pragma endregion

static PyObject* chivel_load(PyObject* self, PyObject* args) {
	const char* path;
	int color_space = COLOR_SPACE_BGR; // Default to BGR
	if (!PyArg_ParseTuple(args, "s|i", &path, &color_space))
		return nullptr;

	// Load image using OpenCV
	cv::Mat img = chivel::readImage(path, static_cast<ColorSpace>(color_space));
	if (img.empty()) {
		PyErr_SetString(PyExc_IOError, "Failed to load image from path");
		return nullptr;
	}

	// Convert to the correct color space
	ColorSpace current;
	switch (color_space)
	{
	case COLOR_SPACE_BGRA:
	case COLOR_SPACE_RGBA:
		current = COLOR_SPACE_BGRA;
		break;
	case COLOR_SPACE_GRAY:
		current = COLOR_SPACE_GRAY;
		break;
	default:
		current = COLOR_SPACE_BGR;
		break;
	}
	img = chivel::convertColorSpace(img, current, static_cast<ColorSpace>(color_space));

	// Create a new chivel.Image object
	PyObject* image_obj = CHIVELImage_new(&CHIVELImageType, nullptr, nullptr);
	if (!image_obj)
		return nullptr;

	CHIVELImageObject* image = (CHIVELImageObject*)image_obj;
	delete image->mat; // Delete the default empty mat
	image->mat = new cv::Mat(img); // Assign loaded image
	image->color_space = static_cast<ColorSpace>(color_space);

	return image_obj;
}

cv::Mat readImage(char const* const path, int color_space = COLOR_SPACE_BGR)
{
	int imread_flag = cv::IMREAD_COLOR;
	switch (color_space) {
	case COLOR_SPACE_BGR: imread_flag = cv::IMREAD_COLOR; break;
	case COLOR_SPACE_BGRA: imread_flag = cv::IMREAD_UNCHANGED; break;
	case COLOR_SPACE_RGB: imread_flag = cv::IMREAD_COLOR; break; // Will need conversion after load
	case COLOR_SPACE_RGBA: imread_flag = cv::IMREAD_UNCHANGED; break; // Will need conversion after load
	case COLOR_SPACE_GRAY: imread_flag = cv::IMREAD_GRAYSCALE; break;
	default: imread_flag = cv::IMREAD_COLOR; break;
	}
	cv::Mat img = cv::imread(path, imread_flag);
	if (img.empty()) return img;
	// Convert if needed
	if (color_space == COLOR_SPACE_RGB && img.channels() == 3)
		cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
	else if (color_space == COLOR_SPACE_RGBA && img.channels() == 4)
		cv::cvtColor(img, img, cv::COLOR_BGRA2RGBA);
	return img;
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

static PyObject* chivel_capture(PyObject* self, PyObject* args, PyObject* kwargs) {
	PyObject* rect_obj = nullptr;
	int displayIndex = 0;
	static const char* kwlist[] = { "display_index", "rect", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|iO", (char**)kwlist, &displayIndex, &rect_obj))
		return nullptr;

	cv::Mat img;
	if (rect_obj && rect_obj != Py_None) {
		// Expect a chivel.Rect object
		if (!PyObject_TypeCheck(rect_obj, &CHIVELRectType)) {
			PyErr_SetString(PyExc_TypeError, "rect must be a chivel.Rect object");
			return nullptr;
		}
		CHIVELRectObject* rect = (CHIVELRectObject*)rect_obj;
		int x = rect->x;
		int y = rect->y;
		int w = rect->width;
		int h = rect->height;
		if (w <= 0 || h <= 0) {
			PyErr_SetString(PyExc_ValueError, "Width and height must be positive");
			return nullptr;
		}
		img = chivel::captureRect(x, y, w, h, displayIndex);
	}
	else {
		img = chivel::captureScreen(displayIndex);
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
	image->color_space = COLOR_SPACE_DEFAULT;

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

static PyObject* chivel_find_image(PyObject* self, PyObject* args, PyObject* kwargs) {
   PyObject* source_obj;
   PyObject* search_obj;
   double threshold = 0.8; // Default threshold for match quality

   static const char* kwlist[] = { "source", "search", "threshold" };
   if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|d", (char**)kwlist, &source_obj, &search_obj, &threshold))
       return nullptr;

   if (!PyObject_TypeCheck(source_obj, &CHIVELImageType)) {
       PyErr_SetString(PyExc_TypeError, "First argument must be a chivel.Image object");
       return nullptr;
   }

   if (!PyObject_TypeCheck(search_obj, &CHIVELImageType)) {
       PyErr_SetString(PyExc_TypeError, "Second argument must be a chivel.Image object");
       return nullptr;
   }

   CHIVELImageObject* source = (CHIVELImageObject*)source_obj;
   if (!source->mat || source->mat->empty()) {
       PyErr_SetString(PyExc_ValueError, "Source image is empty");
       return nullptr;
   }

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
       // Create a chivel.Rect object
       PyObject* rect_obj = create_rect(r.x, r.y, r.width, r.height);

       // Create a chivel.Match object
       PyObject* match_obj = create_match(rect_obj);

       PyList_Append(matches, match_obj);
       Py_DECREF(rect_obj);
       Py_DECREF(match_obj);
   }
   return matches;
}

static PyObject* chivel_find_text(PyObject* self, PyObject* args, PyObject* kwargs) {  
   PyObject* source_obj;
   const char* search_str;
   double threshold = 0.0; // Default threshold for match quality
   int level = tesseract::RIL_PARA; // Default to PARA

   static const char* kwlist[] = { "source", "search", "threshold", "text_level", nullptr };  
   if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Os|di", (char**)kwlist, &source_obj, &search_str, &threshold, &level))  
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

   // Perform OCR and search for the text  
   std::string search_trimmed = chivel::trim(search_str);  
   std::regex search_regex(search_trimmed);  

   cv::Mat original = *(source->mat);  
   int width = original.cols;  
   int height = original.rows;  
   cv::Mat src = chivel::adjustImage(original);  

   tesseract::TessBaseAPI tess;  
   std::filesystem::path tessdata_path = get_module_dir() / "tessdata";  
   if (tess.Init(tessdata_path.string().c_str(), "eng", tesseract::OEM_LSTM_ONLY) != 0) {  
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
           if (word) {  
               delete[] word; // Clean up the allocated memory  
           }  

           float conf = ri->Confidence(pil);  
           if (word_str.empty() || conf < threshold * 100.0f) {  
               continue;  
           }  
           // Scale bounding box coordinates  
           int x1, y1, x2, y2;  
           if (ri->BoundingBox(pil, &x1, &y1, &x2, &y2)) {  
               x1 = static_cast<int>(x1 * scaleX);  
               y1 = static_cast<int>(y1 * scaleY);  
               x2 = static_cast<int>(x2 * scaleX);  
               y2 = static_cast<int>(y2 * scaleY);  
               std::smatch word_match;  
               if (std::regex_match(word_str, word_match, search_regex)) {  
                   // Create a chivel.Rect object  
                   PyObject* rect_obj = create_rect(x1, y1, x2 - x1, y2 - y1);  
                   if (!rect_obj) {  
                       return nullptr; // Error creating rect object  
                   }  

                   // Create a chivel.Match object  
                   PyObject* match_obj = create_match(rect_obj, PyUnicode_FromString(word_str.c_str()));  
                   if (!match_obj) {  
                       Py_DECREF(rect_obj);  
                       return nullptr; // Error creating match object  
                   }  

                   PyList_Append(matches, match_obj);  
                   Py_DECREF(rect_obj);  
                   Py_DECREF(match_obj);  
               }  
           }  
       } while (ri->Next(pil));  
   }  

   return matches;  
}

static PyObject* chivel_mouse_move(PyObject* self, PyObject* args, PyObject* kwds) {
	PyObject* pos_obj = nullptr;
	int display_index = 0;
	static const char* kwlist[] = { "pos", "display_index", nullptr };

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|i", (char**)kwlist, &pos_obj, &display_index))
		return nullptr;

	// Get monitor info
	DISPLAY_DEVICE dd;
	dd.cb = sizeof(dd);
	if (!EnumDisplayDevices(NULL, display_index, &dd, 0)) {
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
	bool found = false;

	// Accept chivel.Point, chivel.Rect, chivel.Match, or tuple
	if (PyObject_TypeCheck(pos_obj, &CHIVELPointType)) {
		CHIVELPointObject* pt = (CHIVELPointObject*)pos_obj;
		x = pt->x;
		y = pt->y;
		found = true;
	}
	else if (PyObject_TypeCheck(pos_obj, &CHIVELRectType)) {
		CHIVELRectObject* rect = (CHIVELRectObject*)pos_obj;
		x = rect->x + rect->width / 2;
		y = rect->y + rect->height / 2;
		found = true;
	}
	else if (PyObject_TypeCheck(pos_obj, &CHIVELMatchType)) {
		CHIVELMatchObject* match = (CHIVELMatchObject*)pos_obj;
		if (match->rect && PyObject_TypeCheck(match->rect, &CHIVELRectType)) {
			CHIVELRectObject* rect = (CHIVELRectObject*)match->rect;
			x = rect->x + rect->width / 2;
			y = rect->y + rect->height / 2;
			found = true;
		}
		else {
			PyErr_SetString(PyExc_TypeError, "Match.rect must be a chivel.Rect object");
			return nullptr;
		}
	}

	if (!found) {
		PyErr_SetString(PyExc_TypeError, "Position argument must be a chivel.Point, chivel.Rect, chivel.Match");
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
	int button = BUTTON_LEFT;
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
	int button = BUTTON_LEFT;
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
	int button = BUTTON_LEFT;
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
	static const char* kwlist[] = { "output_path", "simplify", "stop_key", nullptr };
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

	if (!script_path)
	{
		Py_RETURN_NONE;
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
	POINT pt = chivel::getCursorPosition();

	// Find which monitor contains the point
	struct MonitorSearch {
		POINT pt;
		int index;
		int found;
		int x, y;
	} search = { pt, 0, -1, 0, 0 };

	EnumDisplayMonitors(nullptr, nullptr, chivel::monitor_enum_proc, reinterpret_cast<LPARAM>(&search));

	PyObject* point_obj = create_point(pt.x, pt.y);
	if (!point_obj)
		return nullptr;

	int display_index = search.found;

	return Py_BuildValue("(Oi)", point_obj, display_index);
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
	int display_index = 0;
	if (!PyArg_ParseTuple(args, "|i", &display_index))
		return nullptr;

	DISPLAY_DEVICE dd;
	dd.cb = sizeof(dd);
	if (!EnumDisplayDevices(NULL, display_index, &dd, 0)) {
		PyErr_SetString(PyExc_ValueError, "Invalid display index");
		return nullptr;
	}

	DEVMODE dm;
	dm.dmSize = sizeof(dm);
	if (!EnumDisplaySettings(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
		PyErr_SetString(PyExc_RuntimeError, "Failed to get display settings");
		return nullptr;
	}

	// dmPosition is relative to the primary display
	int x = dm.dmPosition.x;
	int y = dm.dmPosition.y;
	int width = dm.dmPelsWidth;
	int height = dm.dmPelsHeight;

	// Return a chivel.Rect object instead of a tuple
	PyObject* rect_obj = CHIVELRect_new(&CHIVELRectType, nullptr, nullptr);
	if (!rect_obj)
		return nullptr;
	CHIVELRectObject* rect = (CHIVELRectObject*)rect_obj;
	rect->x = x;
	rect->y = y;
	rect->width = width;
	rect->height = height;
	return rect_obj;
}

// Module initialization
static int chivel_module_exec(PyObject* module)
{
	// do not print openCV stuff
	cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);

	// make process DPI aware for mouse scaling
	SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

	if (PyType_Ready(&CHIVELRectType) < 0)
		return -1;
	Py_INCREF(&CHIVELRectType);
	if (PyModule_AddObject(module, "Rect", (PyObject*)&CHIVELRectType) < 0) {
		Py_DECREF(&CHIVELRectType);
		return -1;
	}

	if (PyType_Ready(&CHIVELPointType) < 0)
		return -1;
	Py_INCREF(&CHIVELPointType);
	if (PyModule_AddObject(module, "Point", (PyObject*)&CHIVELPointType) < 0) {
		Py_DECREF(&CHIVELPointType);
		return -1;
	}

	if (PyType_Ready(&CHIVELColorType) < 0)
		return -1;
	Py_INCREF(&CHIVELColorType);
	if (PyModule_AddObject(module, "Color", (PyObject*)&CHIVELColorType) < 0) {
		Py_DECREF(&CHIVELColorType);
		return -1;
	}

	if (PyType_Ready(&CHIVELMatchType) < 0)
		return -1;
	Py_INCREF(&CHIVELMatchType);
	if (PyModule_AddObject(module, "Match", (PyObject*)&CHIVELMatchType) < 0) {
		Py_DECREF(&CHIVELMatchType);
		return -1;
	}

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

	PyModule_AddIntConstant(module, "BUTTON_LEFT", BUTTON_LEFT);
	PyModule_AddIntConstant(module, "BUTTON_RIGHT", BUTTON_RIGHT);
	PyModule_AddIntConstant(module, "BUTTON_MIDDLE", BUTTON_MIDDLE);
	PyModule_AddIntConstant(module, "BUTTON_X1", BUTTON_X1);
	PyModule_AddIntConstant(module, "BUTTON_X2", BUTTON_X2);

	PyModule_AddIntConstant(module, "SIMPLIFY_NONE", SIMPLIFY_NONE);
	PyModule_AddIntConstant(module, "SIMPLIFY_MOVE", SIMPLIFY_MOVE);
	PyModule_AddIntConstant(module, "SIMPLIFY_CLICK", SIMPLIFY_CLICK);
	PyModule_AddIntConstant(module, "SIMPLIFY_TYPE", SIMPLIFY_TYPE);
	PyModule_AddIntConstant(module, "SIMPLIFY_TIME", SIMPLIFY_TIME);
	PyModule_AddIntConstant(module, "SIMPLIFY_ALL", SIMPLIFY_ALL);

	PyModule_AddIntConstant(module, "FLIP_HORIZONTAL", FLIP_HORIZONTAL);
	PyModule_AddIntConstant(module, "FLIP_VERTICAL", FLIP_VERTICAL);
	PyModule_AddIntConstant(module, "FLIP_BOTH", FLIP_BOTH);

	PyModule_AddIntConstant(module, "COLOR_SPACE_UNKNOWN", COLOR_SPACE_UNKNOWN);
	PyModule_AddIntConstant(module, "COLOR_SPACE_BGR", COLOR_SPACE_BGR);
	PyModule_AddIntConstant(module, "COLOR_SPACE_BGRA", COLOR_SPACE_BGRA);
	PyModule_AddIntConstant(module, "COLOR_SPACE_RGB", COLOR_SPACE_RGB);
	PyModule_AddIntConstant(module, "COLOR_SPACE_RGBA", COLOR_SPACE_RGBA);
	PyModule_AddIntConstant(module, "COLOR_SPACE_GRAY", COLOR_SPACE_GRAY);
	PyModule_AddIntConstant(module, "COLOR_SPACE_HSV", COLOR_SPACE_HSV);

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
	{"capture", (PyCFunction)chivel_capture, METH_VARARGS | METH_KEYWORDS, "Capture the screen or a specific rectangle"},
	{"find_image", (PyCFunction)chivel_find_image, METH_VARARGS | METH_KEYWORDS, "Find images within an image"},
	{"find_text", (PyCFunction)chivel_find_text, METH_VARARGS | METH_KEYWORDS, "Find text within an image"},
	{"wait", chivel_wait, METH_VARARGS, "Wait for a specified number of seconds"},
	{"mouse_move", (PyCFunction)chivel_mouse_move, METH_VARARGS | METH_KEYWORDS, "Move the mouse cursor to a specific position or rectangle on a display"},
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
