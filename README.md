# os-proj

This is our project for the OS exam 2021/22, details in the pdf file. 

We did the **minimum version** because of lack of time, but we got the max score.

## Getting Started

### Compiling and setting env. variables

Clone this repository
```sh 
git clone https://gitlab2.educ.di.unito.it/st203699/os-proj.git
cd os-proj
```
You can compile with the `Make` utility and use some parameters to change the program's behavior. 

To compile the `master` program with the `SO_BLOCK_SIZE` set to 100 and `SO_REGISTRY_SIZE` set to 1000 as described in the first configuration of the project found in the pdf.
```sh
make all cfg=1
```
The second configuration is `conf2` which sets `SO_BLOCK_SIZE` to 10 and `SO_REGISTRY_SIZE` to 10000.
```sh
make all cfg=2
```
The third configuration is `conf3` which sets `SO_BLOCK_SIZE` to 10 and `SO_REGISTRY_SIZE` to 1000.
```sh
make all cfg=3
```
Of course, there is a way to compile the `master` program with custom values. To do so, you must edit line 4 and 5 of the `makefile` and launch `make all`.
```makefile
###############################
# Custom configuration values #
###############################
SO_BLOCK_SIZE = [CHANGE_THIS]
SO_REGISTRY_SIZE = [CHANGE_THIS]
...
```
```sh
make all
```
The default C Compiler Flags are `-std=c89 -pedantic -O2`, but it's possible to compile the `master` program for debugging purposes with `make all debug=1`. 

It will change the C Compiler Flags to `-std=c89 -pedantic -O0 -g`, it will define the `DEBUG` macro and **it will use the custom configuration parameters**.
To clean the output of the `make` utility, you can run `make clean` which will remove all the object files from the project directory.
```sh
make clean
```
You can also try more complex configurations:
```sh
make all debug=1 cfg=2
```

### Executing program
Before running the program, you must load the environment variables that will be checked at run time. To do so, we'll use the `source` utility from `bash`, `zsh`, ..., but unfortunately it doesn't exist in the `sh` shell. 

If you have the `source` command, run
```sh
source cfg/<conf_file>.cfg
make run
```
Otherwise, you have to manually append the contents of a configuration file that you can find in the `cfg/` directory into `~/.shrc` or `~/.bashrc` or `~/.zshrc` file and then run the following command (Maybe you must restart the terminal in this case).
```sh
make run
```

## Authors

* [Filippo Bogetti](https://bogeee.github.io/)
* Roberto Carletto

## License

This project is licensed under the MIT License - see the [LICENSE.md](./LICENSE.md) file for details

## Acknowledgments

We are coding in C, so there is no reason why we shouldn't follow the Linux Kernel coding style. 

* [Linux Kernel Coding Style](https://www.kernel.org/doc/html/latest/process/coding-style.html)