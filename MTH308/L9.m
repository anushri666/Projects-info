%% Problem 1
% This problem uses the Composite Trapezoidal rule and Composite Simpson's 
% rule to approximate a definite integral of functions.

n = 6;
x = linspace(exp(1), exp(1)+2, n+1);
f = @(x) 1./(x.*log(x));

T = CompositeTrapezoidalRule(x, f(x));
S = CompositeSimpsonsRule(x, f(x));

% Exact value
I_exact = log(log(exp(1)+2));

fprintf('Composite Trapezoidal Rule: %.6f\n', T)
fprintf('Composite Simpson''s Rule: %10.6f\n', S)
fprintf('Exact Value: %10.6f\n\n', I_exact)

% Errors
fprintf('Absolute Error (Trapezoidal): %.6e\n', abs(T - I_exact))
fprintf('Absolute Error (Simpson):     %.6e\n', abs(S - I_exact))


%=================================Functions===============================%
function approx = CompositeTrapezoidalRule(x, f)
%uses the Composite Trapezoidal rule to approximate the value of the
%definite integral given vectors of evenly spaced x values and 
%corresponding function values

%INPUT
%  x: vector of evenly spaced x values
%  f: vector of corresponding function values

%OUTPUT
%  approx: approximation for the definite integral

    %defines step size
    h = (x(end)-x(1))/(length(x)-1); 

    %calculates approximation using the Composite Trapezoidal rule
    approx = h/2*(f(1)+2*sum(f(2:end-1))+f(end)); 
end
%
function approx = CompositeSimpsonsRule(x, f)
%uses the Composite Simpson's rule to approximate the value of the definite
%integral given vectors of evenly spaced x values and corresponding 
%function values

%INPUT
%  x: vector of evenly spaced x values
%  f: vector of corresponding function values

%OUTPUT
%  approx: approximation for the definite integral

    %defines step size
    h = (x(end)-x(1))/(length(x)-1);
    
    %calculates approximation using the Composite Simpson's rule
    approx = 1/3*h*(f(1)+4*sum(f(2:2:end-1))+2*sum(f(3:2:end-1))+f(end));
end
%=========================================================================%