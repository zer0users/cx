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
