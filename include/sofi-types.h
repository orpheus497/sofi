#ifndef INCLUDE_SOFI_TYPES_H
#define INCLUDE_SOFI_TYPES_H
#include <glib.h>
#include <stdint.h>
G_BEGIN_DECLS

/**
 * Type of property
 */
typedef enum {
  /** Integer */
  P_INTEGER,
  /** Double */
  P_DOUBLE,
  /** String */
  P_STRING,
  /** Boolean */
  P_BOOLEAN,
  /** Color */
  P_COLOR,
  /** Image */
  P_IMAGE,
  /** SofiPadding */
  P_PADDING,
  /** Link to global setting */
  P_LINK,
  /** Position */
  P_POSITION,
  /** Highlight */
  P_HIGHLIGHT,
  /** List */
  P_LIST,
  /** Orientation */
  P_ORIENTATION,
  /** Cursor */
  P_CURSOR,
  /** Inherit */
  P_INHERIT,
  /** Number of types. */
  P_NUM_TYPES,
} PropertyType;

/**
 * This array maps PropertyType to a user-readable name.
 * It is important this is kept in sync.
 */
extern const char *const PropertyTypeName[P_NUM_TYPES];

/** Style of text highlight */
typedef enum {
  /** no highlight */
  SOFI_HL_NONE = 0,
  /** bold */
  SOFI_HL_BOLD = 1,
  /** underline */
  SOFI_HL_UNDERLINE = 2,
  /** strikethrough */
  SOFI_HL_STRIKETHROUGH = 16,
  /** italic */
  SOFI_HL_ITALIC = 4,
  /** color */
  SOFI_HL_COLOR = 8,
  /** uppercase */
  SOFI_HL_UPPERCASE = 32,
  /** lowercase */
  SOFI_HL_LOWERCASE = 64,
  /** capitalize */
  SOFI_HL_CAPITALIZE = 128
} SofiHighlightStyle;

/** Style of line */
typedef enum {
  /** Solid line */
  SOFI_HL_SOLID,
  /** Dashed line */
  SOFI_HL_DASH
} SofiLineStyle;

/**
 * Distance unit type.
 */
typedef enum {
  /** PixelWidth in pixels. */
  SOFI_PU_PX,
  /** PixelWidth in millimeters. */
  SOFI_PU_MM,
  /** PixelWidth in EM. */
  SOFI_PU_EM,
  /** PixelWidget in percentage */
  SOFI_PU_PERCENT,
  /** PixelWidth in CH. */
  SOFI_PU_CH,
} SofiPixelUnit;

/**
 * Structure representing a distance.
 */
typedef enum {
  SOFI_DISTANCE_MODIFIER_NONE,
  SOFI_DISTANCE_MODIFIER_ADD,
  SOFI_DISTANCE_MODIFIER_SUBTRACT,
  SOFI_DISTANCE_MODIFIER_DIVIDE,
  SOFI_DISTANCE_MODIFIER_MULTIPLY,
  SOFI_DISTANCE_MODIFIER_MODULO,
  SOFI_DISTANCE_MODIFIER_GROUP,
  SOFI_DISTANCE_MODIFIER_MIN,
  SOFI_DISTANCE_MODIFIER_MAX,
  SOFI_DISTANCE_MODIFIER_ROUND,
  SOFI_DISTANCE_MODIFIER_FLOOR,
  SOFI_DISTANCE_MODIFIER_CEIL,
} SofiDistanceModifier;

typedef struct SofiDistanceUnit {
  /** Distance */
  double distance;
  /** Unit type of the distance */
  SofiPixelUnit type;

  /** Type */
  SofiDistanceModifier modtype;

  /** Modifier */
  struct SofiDistanceUnit *left;

  /** Modifier */
  struct SofiDistanceUnit *right;
} SofiDistanceUnit;

typedef struct {
  /** Base */
  SofiDistanceUnit base;
  /** Style of the line (optional)*/
  SofiLineStyle style;
} SofiDistance;

/**
 * Type of orientation.
 */
typedef enum {
  SOFI_ORIENTATION_VERTICAL,
  SOFI_ORIENTATION_HORIZONTAL
} SofiOrientation;

/**
 * Cursor type.
 */
typedef enum {
  SOFI_CURSOR_DEFAULT,
  SOFI_CURSOR_POINTER,
  SOFI_CURSOR_TEXT
} SofiCursorType;

/**
 * Represent the color in theme.
 */
typedef struct {
  /** red channel */
  double red;
  /** green channel */
  double green;
  /** blue channel */
  double blue;
  /**  alpha channel */
  double alpha;
} ThemeColor;

/**
 * Theme Image
 */
typedef enum { SOFI_IMAGE_URL, SOFI_IMAGE_LINEAR_GRADIENT } SofiImageType;

typedef enum {
  SOFI_DIRECTION_LEFT,
  SOFI_DIRECTION_RIGHT,
  SOFI_DIRECTION_TOP,
  SOFI_DIRECTION_BOTTOM,
  SOFI_DIRECTION_ANGLE,
} SofiDirection;

typedef enum {
  SOFI_SCALE_NONE,
  SOFI_SCALE_BOTH,
  SOFI_SCALE_HEIGHT,
  SOFI_SCALE_WIDTH,
} SofiScaleType;

typedef struct {
  SofiImageType type;
  char *url;
  SofiScaleType scaling;
  int wsize;
  int hsize;

  SofiDirection dir;
  double angle;
  /** colors */
  GList *colors;

  /** cached image */
  uint32_t surface_id;

} SofiImage;

/**
 * SofiPadding
 */
typedef struct {
  SofiDistance top;
  SofiDistance right;
  SofiDistance bottom;
  SofiDistance left;
} SofiPadding;

/**
 * Theme highlight.
 */
typedef struct {
  /** style to display */
  SofiHighlightStyle style;
  /** Color */
  ThemeColor color;
} SofiHighlightColorStyle;

/**
 * Enumeration indicating location or gravity of window.
 *
 * \verbatim WL_NORTH_WEST      WL_NORTH      WL_NORTH_EAST \endverbatim
 * \verbatim WL_EAST            WL_CENTER     WL_EAST \endverbatim
 * \verbatim WL_SOUTH_WEST      WL_SOUTH      WL_SOUTH_EAST\endverbatim
 *
 * @ingroup CONFIGURATION
 */
typedef enum {
  /** Center */
  WL_CENTER = 0,
  /** Top middle */
  WL_NORTH = 1,
  /** Middle right */
  WL_EAST = 2,
  /** Bottom middle */
  WL_SOUTH = 4,
  /** Middle left */
  WL_WEST = 8,
  /** Left top corner. */
  WL_NORTH_WEST = WL_NORTH | WL_WEST,
  /** Top right */
  WL_NORTH_EAST = WL_NORTH | WL_EAST,
  /** Bottom right */
  WL_SOUTH_EAST = WL_SOUTH | WL_EAST,
  /** Bottom left */
  WL_SOUTH_WEST = WL_SOUTH | WL_WEST,
} WindowLocation;

typedef union _PropertyValue {
  /** integer */
  int i;
  /** Double */
  double f;
  /** String */
  char *s;
  /** boolean */
  gboolean b;
  /** Color */
  ThemeColor color;
  /** SofiPadding */
  SofiPadding padding;
  /** Reference */
  struct {
    /** Name */
    char *name;
    /** Cached looked up ref */
    struct Property *ref;
    /** Property default */
    struct Property *def_value;
  } link;
  /** Highlight Style */
  SofiHighlightColorStyle highlight;
  /** Image */
  SofiImage image;
  /** List */
  GList *list;
} PropertyValue;

/**
 * Property structure.
 */
typedef struct Property {
  /** Name of property */
  char *name;
  /** Type of property. */
  PropertyType type;
  /** Value */
  PropertyValue value;
} Property;

/**
 * Describe the media constraint type.
 */
typedef enum {
  /** Minimum width constraint. */
  THEME_MEDIA_TYPE_MIN_WIDTH,
  /** Maximum width constraint. */
  THEME_MEDIA_TYPE_MAX_WIDTH,
  /** Minimum height constraint. */
  THEME_MEDIA_TYPE_MIN_HEIGHT,
  /** Maximum height constraint. */
  THEME_MEDIA_TYPE_MAX_HEIGHT,
  /** Monitor id constraint. */
  THEME_MEDIA_TYPE_MON_ID,
  /** Minimum aspect ratio constraint. */
  THEME_MEDIA_TYPE_MIN_ASPECT_RATIO,
  /** Maximum aspect ratio constraint. */
  THEME_MEDIA_TYPE_MAX_ASPECT_RATIO,
  /** Boolean option for use with env. */
  THEME_MEDIA_TYPE_BOOLEAN,
  /** Invalid entry. */
  THEME_MEDIA_TYPE_INVALID,
} ThemeMediaType;

/**
 * Theme Media description.
 */
typedef struct ThemeMedia {
  ThemeMediaType type;
  double value;
  gboolean boolv;
} ThemeMedia;

/**
 * ThemeWidget.
 */
typedef struct ThemeWidget {
  int set;
  char *name;

  unsigned int num_widgets;
  struct ThemeWidget **widgets;

  ThemeMedia *media;

  GHashTable *properties;

  struct ThemeWidget *parent;
} ThemeWidget;

typedef ThemeWidget ConfigEntry;
/**
 * Structure to hold a range.
 */
typedef struct sofi_range_pair {
  int start;
  int stop;
} sofi_range_pair;

/**
 * Internal structure for matching.
 */
typedef struct sofi_int_matcher_t {
  GRegex *regex;
  gboolean invert;
} sofi_int_matcher;

/**
 * Structure with data to process by each worker thread.
 * TODO: Make this more generic wrapper.
 */
typedef struct _thread_state {
  void (*callback)(struct _thread_state *t, gpointer data);
  void (*free)(void *);
  int priority;
} thread_state;

extern GThreadPool *tpool;

G_END_DECLS
#endif // INCLUDE_SOFI_TYPES_H
