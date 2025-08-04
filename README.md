# cx
With the help of Jehovah..

We have made CX.

### How to install

```bash
curl -s https://raw.githubusercontent.com/zer0users/cx/main/install.sh | bash
```

### Getting started with love

```bash
mkdir MyProject
cd MyProject
```

In the folder **MyProject** (Or another folder), create an main.cx file with the following content.

```cx
program "MyProgram"

#get "shell"
#get "cx"
#from "cx" get "cx-app"
#from "cx" get "cx-project"

define class "MyProgram"
    cx.project.name = "MyApplication"
    cx.project.platform = "linux"
    cx.app.shell = "python3"
    cx.app.class = "Application"
finish

define class "Application"
    print('Welcome to your Application')
finish
```

For **compile** your first app, execute:

```bash
cx build
```

On the main.cx folder.

It will return an **MyApplication.cxA** file, Run it:


```bash
cx MyApplication.cxA
```

### Questions

Question: Is **cx** a **malware**?

Anwser: No, cx is not a malware, in reality, an .cxA created by someone CAN be a malware, Because cx creates and compiles .cx files to an executable (.cxA), cx will run the code in the .cxA, so please! Do not run .cxA that do not you trust, cx is a open-source project, you can check the code on "cx_build.c".

Question: If **cx** cannot be malware, the **install.sh** can be malware?

Anwser: No, the installer has steps for install cx

Step 1. Download "cx_build.c" - The installer downloads first the source code
Step 2. Compile "cx_build.c" to an executable - After the installer downloads the source code, it executes "gcc -o cx cx_build.c -lz" to compile it to an executable.
Step 3. Request sudo permissions to move it into "/usr/bin" - The installer moves the compiled executable to the "/usr/bin" folder, that are the programs.
Step 4. Finish - The installer finishes with an Loving message!
