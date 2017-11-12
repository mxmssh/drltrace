# Description

The script allows to perform visualization of a trace of library calls provided in the following format 
(arguments are ignored):

*<library_name>!<library_call_name>*

Each unique library call is assigned with unique color from RGB or grayscale pallette. The tool generates an 
image where each pixel represents library call from a trace.

**Example:**

While the script was mainly developed to visualize an output of drltrace, the script is possible to use for custom traces provided in the appropriate format.

# Dependencies

matplotlib

Pillow

# Usage
```api_calls_vis.py -i [jpeg image name] -ht [html image name] -gr -t [trace_name]```

Arguments:

```  -h, --help            show the help message and exit```

```  -t TRACE, --trace TRACE A trace file with API calls ```

```  -i IMAGE, --image IMAGE A name of image file ```

```  -ht HTML, --html HTML A name of html page (heavy) ```

```  -gr, --grayscale      Generate an image in grayscale ```

**Example:** ```api_calls_vis.py -i calc.jpeg -ht calc.html -gr -t drltrace.calc.exe.00864.0000.log```
