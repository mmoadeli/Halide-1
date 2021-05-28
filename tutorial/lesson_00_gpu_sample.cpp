#include <iostream>
#include <iomanip>

#include "Halide.h"
#include "halide_benchmark.h"
#include "float_comparison.h"

using namespace std;
using namespace Halide;
using namespace Halide::ConciseCasts;
using namespace Halide::Tools;

ImageParam A(Float(16), 2, "A");
ImageParam B(Float(16), 2, "B");
Func out("matmul");

const bool test_single = true;
const bool verbose = true;
const bool print_outputs = true;
const bool print_to_file = false;
const bool try_schedules = false;

template<typename T>
void InitMatrix(Buffer<T>& buffer)
{
	const int rows = buffer.height();
	const int cols = buffer.width();
	for (int y = 0; y < rows; ++y)
	{
		for (int x = 0; x < cols; ++x)
		{
			float v = x + y * cols;
			v /= rows * cols;
			// v = 1.0f;
			// std::cout << x << ", " << y << " = " << v << std::endl;
			buffer(x, y) = static_cast<T>(v);
		}
	}

}

template<typename T>
void PrintBuffer(const Buffer<T>& buffer, const std::string& filename = "buffer.txt")
{
	FILE* file = nullptr;
	if (print_to_file)
	{
		file = fopen(filename.c_str(), "w");
	}

	// Print the result.
	const int rows = buffer.height();
	const int cols = buffer.width();
	for (int y = 0; y < rows; ++y)
	{
		for (int x = 0; x < cols; ++x)
		{
			printf("%5.2f ", (float)buffer(x, y));
			if (print_to_file)
			{
				fprintf(file, "%5.2f ", (float)buffer(x, y));
			}
		}
		printf("\n");
		if (print_to_file)
		{
			fprintf(file, "\n");
		}
	}

	if (print_to_file)
	{
		fclose(file);
	}
}


// mk x kn
// rows (M) x columns (N)
void TestMatMul(int M, int N, int K)
{
	std::cout << "M: " << M << " N: " << N << " K: " << K << std::endl;

	try
	{
		const int x_tile = 16;
		const int y_tile = 16;

		Target target = get_host_target();
		Target jitTarget = get_jit_target_from_environment();

		// Enable the CUDA target
		target.set_feature(Target::CUDA);
		target.set_feature(Target::CUDACapability70);

		if (verbose)
		{
			// Enable debugging hopefully to see the CUDA API calls
			target.set_feature(Target::Debug);

			cout << "Halide Host Target: " << target << endl;
			cout << "Halide JIT Target : " << jitTarget << endl;
		}

		Var x("x"), y("y");

		Func matmul("matmul");
		RDom k(0, A.dim(0).extent(), "k");
		// RDom k(0, K, "k");
		matmul(x, y) += f32(A(k, y)) * f32(B(x, k));

		out = matmul;

		Var blockX("blockX"), blockY("blockY"), threadX("threadX"), threadY("threadY");
		if (target.has_gpu_feature())
		{
			out
				// .gpu_tile(x, y, blockX, blockY, threadX, threadY, x_tile, y_tile, TailStrategy::Auto)
				;

			Var xi("xi");
			Var xo("xo");
			Var yi("yi");
			Var yo("yo");
			RVar ki("ki");
			RVar ko("ko");
			out
				.update()
				.split(k, ko, k, 16)
				.gpu_tile(x, y, blockX, blockY, threadX, threadY, 16, 16, TailStrategy::Auto)

				/*.tile(x, y, blockX, blockY, threadX, threadY, x_tile, y_tile)
				.gpu_blocks(blockX, blockY)
				.gpu_threads(threadX, threadY)*/
				;
		}

		if (verbose)
		{
			cout << "Matmul Loop Nest: " << std::endl;
			out.print_loop_nest();
			cout << endl << endl;

			// Internal::Stmt s = Internal::lower_main_stmt({ out.function() }, out.name(), target);
			// Internal::MyIRVisitor vis(std::cout);
			// s.accept(&vis);
		}

		std::vector<Argument> args = { A, B };

		if (verbose)
		{
			cout << "Compiling to HTML ... " << flush;
			out.compile_to_lowered_stmt("cuda.html", args, HTML, target);
			// matmul.compile_to_lowered_stmt("cuda.html", {}, HTML, target);
			cout << "done" << endl;

			cout << "Compiling to LLVM ... " << flush;
			// out.compile_to_llvm_assembly("cuda.ll", args, target);
			cout << "done" << endl;

			cout << "Compiling to Assembly ... " << flush;
			//out.compile_to_assembly("cuda.S", {}, target);
			cout << "done" << endl;

			cout << "Jitting the code ... " << flush;
			out.compile_jit(target);
			cout << "done" << endl << endl;
		}

		Buffer<float16_t> inputA(K, M);
		Buffer<float16_t> inputB(N, K);

		InitMatrix(inputA);
		InitMatrix(inputB);

		A.set(inputA);
		B.set(inputB);

		if (verbose && print_outputs)
		{
			cout << "Input A: (" << inputA.height() << " x " << inputA.width() << ")" << endl;
			PrintBuffer(inputA, "inputA.txt");
			cout << endl << endl;

			cout << "Input B: (" << inputB.height() << " x " << inputB.width() << ")" << endl;
			PrintBuffer(inputB, "inputB.txt");
			cout << endl << endl;
		}

		// Run it
		Buffer<float> result(N, M);

		out.realize(result, target);

		auto start = std::chrono::high_resolution_clock::now();
		for (int i = 0; i < 1; ++i)
		{
			out.realize(result, target);
			result.device_sync();
		}
		auto end = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double, std::ratio<1, 1000>> elapsed_milliseconds = end - start;
		std::cout << "std::chrono Time: " << (elapsed_milliseconds.count() / 5) << " ms" << std::endl;

		double t = benchmark(1, 1, [&]
			{
				out.realize(result, target);
				result.device_sync();
			});
		std::cout << "Halide Time: " << (t * 1000.0f) << " ms" << std::endl;

		result.copy_to_host();

		if (verbose && print_outputs)
		{
			cout << "Result: (" << result.height() << " x " << result.width() << ")" << endl;
			PrintBuffer(result, "result.txt");
			cout << endl << endl;
		}

		// cout << "Comparing: " << endl;
		for (int y = 0; y < result.height(); ++y)
		{
			for (int x = 0; x < result.width(); ++x)
			{
				float acc = 0.0f;
				for (int k = 0; k < K; ++k)
				{
					float16_t a = inputA(k, y);
					float16_t b = inputB(x, k);
					acc += static_cast<float>(a) * static_cast<float>(b);
				}
				const std::string result_str = "result(" + std::to_string(x) + ", " + std::to_string(y) + ")";
				if (!expect_almost_equal(result_str.c_str(), "acc", "aa", result(x, y), acc, 200))
				{
					exit(1);
				}
			}
		}
	}
	catch (RuntimeError& e)
	{
		cout << e.what() << endl;
	}
	catch (CompileError& e)
	{
		cout << e.what() << endl;
	}
	catch (InternalError& e)
	{
		cout << e.what() << endl;
	}
}

void TryingSchedules()
{
	try
	{
		Var x("x"), y("y");
		Var xi("xi"), xo("xo");
		Var yi("yi"), yo("yo");

		Func f("f");
		RDom k(0, 16, "k");
		f(x, y) += A(k, y) * B(x, k);

		Func out("out");
		out(x, y) = f(x, y);

		out.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);

		Target target = get_jit_target_from_environment();
		target.set_feature(Target::CUDA);

		Internal::Stmt s = Internal::lower_main_stmt({out.function()}, out.name(), target);
		Internal::IRPrinter p(std::cout);
		s.accept(&p);
		std::cout << std::endl << std::endl;

		out.print_loop_nest();
	}
	catch (RuntimeError& e)
	{
		cout << e.what() << endl;
	}
	catch (CompileError& e)
	{
		cout << e.what() << endl;
	}
	catch (InternalError& e)
	{
		cout << e.what() << endl;
	}
}

int main()
{
	try
	{
		std::cout << "Starting correctness tests ... " << std::endl;

		if (try_schedules)
		{
			TryingSchedules();
		}
		else
		{
			if (test_single)
			{
				// mk x kn = mn
				const int min_size = 32;
				const int max_size = 32;
				const int m = max_size;
				const int n = max_size;
				const int k = max_size;

				for (int s = min_size; s <= max_size; s += 16)
				{
					TestMatMul(s, s, s);
				}
			}
			else
			{
				const int max_size = 1024;
				const int tile_size = 16;
				for (int m = 16; m <= max_size; m += tile_size)
				{
					for (int n = 16; n <= max_size; n += tile_size)
					{
						for (int k = 16; k <= max_size; k += tile_size)
						{
							TestMatMul(m, n, k);
						}
					}
				}
			}
		}

		std::cout << "done" << std::endl;
	}
	catch (RuntimeError& e)
	{
		cout << e.what() << endl;
	}
	catch (CompileError& e)
	{
		cout << e.what() << endl;
	}
	catch (InternalError& e)
	{
		cout << e.what() << endl;
	}

	return 0;
}
